#include "update_task.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

#include <memory>

#include "diag.h"

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_data_cert_x509_crt_bundle_bin_start");

namespace {

constexpr uint32_t kHttpTimeoutMs = 20000;
constexpr uint32_t kStallMs = 25000;       // a download that delivers nothing for this long is given up
// A TLS session allocates two 16 KB record buffers and some smaller blocks: it needs room in total and one big block,
// not one huge block (a second check after the first one has a smaller largest block, and still works).
constexpr uint32_t kMinFreeHeap = 60000;
constexpr uint32_t kMinLargestBlock = 20000;

std::string userAgent() { return std::string("jiggler/") + JIG_VERSION; }

// Opens `url` over HTTPS with the project's certificate bundle. Empty string = ready, else the reason.
std::string open(HTTPClient& http, WiFiClientSecure& client, const std::string& url, bool followRedirects) {
  if (ESP.getFreeHeap() < kMinFreeHeap || ESP.getMaxAllocHeap() < kMinLargestBlock) return "not enough free memory for a secure connection right now; try again in a moment";
  client.setCACertBundle(rootca_crt_bundle_start);
  client.setTimeout(kHttpTimeoutMs / 1000);
  http.setConnectTimeout(kHttpTimeoutMs);
  http.setTimeout(kHttpTimeoutMs);
  http.setUserAgent(userAgent().c_str());
  http.useHTTP10(true);  // no chunked bodies: the body can be read as it is
  if (followRedirects) http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url.c_str())) return "the address could not be used";
  return "";
}

std::string failure(int code) {
  if (code == 404) return "no published release was found there (the repository may be private or have no release)";
  if (code == 403 || code == 429) return "GitHub is limiting requests right now; try again later";
  if (code > 0) return "the server answered HTTP " + std::to_string(code);
  char buf[48];
  snprintf(buf, sizeof buf, "could not connect (%s); is the clock set?", HTTPClient::errorToString(code).c_str());
  return buf;
}

class GithubSource : public jig::ReleaseSource {
 public:
  jig::FetchResult fetchLatest(const std::string& apiUrl) override {
    jig::FetchResult out;
    diagf("Update: checking %s (heap %u, largest block %u)", apiUrl.c_str(), static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
    WiFiClientSecure client;
    HTTPClient http;
    out.error = open(http, client, apiUrl, false);
    if (!out.error.empty()) return out;
    http.addHeader("Accept", "application/vnd.github+json");
    const int code = http.GET();
    diagf("Update: GitHub answered %d (stack left %u)", code, static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    if (code != 200) {
      out.error = failure(code);
      http.end();
      return out;
    }
    JsonDocument filter;
    filter["tag_name"] = true;
    filter["draft"] = true;
    filter["prerelease"] = true;
    filter["html_url"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;
    filter["assets"][0]["size"] = true;
    filter["assets"][0]["digest"] = true;
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    diagf("Update: release read (%s)", err.c_str());
    if (err) {
      out.error = std::string("the release information could not be read (") + err.c_str() + ")";
      return out;
    }
    serializeJson(doc, out.body);
    out.ok = true;
    return out;
  }
};

std::string hostOf(const std::string& url) {
  const size_t start = url.find("://");
  if (start == std::string::npos) return "?";
  const size_t from = start + 3, end = url.find('/', from);
  return url.substr(from, end == std::string::npos ? std::string::npos : end - from);
}

// One HTTPS connection at a time. A release asset is served through a redirect to another host, and holding two TLS
// sessions at once would need more memory than the device has to spare.
struct Connection {
  std::unique_ptr<WiFiClientSecure> client;
  std::unique_ptr<HTTPClient> http;
  void close() {
    if (http) http->end();
    http.reset();  // the HTTP client refers to the connection, so it goes first
    client.reset();
  }
  ~Connection() { close(); }
};

// GET `url`, following up to four redirects. "" = the answer (see `status`) is ready to be read.
std::string getFollowing(Connection& c, std::string url, int& status) {
  for (int hop = 0; hop < 5; ++hop) {
    c.close();
    c.client = std::make_unique<WiFiClientSecure>();
    c.http = std::make_unique<HTTPClient>();
    const std::string err = open(*c.http, *c.client, url, false);
    if (!err.empty()) return err;
    static const char* keys[] = {"Location"};
    c.http->collectHeaders(keys, 1);
    diagf("Update: GET from %s (heap %u, largest block %u)", hostOf(url).c_str(), static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
    status = c.http->GET();
    if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) return "";
    const String location = c.http->header("Location");
    if (location.isEmpty()) return "the server redirected without saying where";
    url = location.c_str();
    if (url.compare(0, 8, "https://") != 0) return "the server redirected to an address that is not secure";
    diagf("Update: redirected (%d) to %s", status, hostOf(url).c_str());
  }
  return "too many redirects";
}

constexpr uint32_t kInstallStack = 20480;  // writing and verifying the image needs far more than a check does

class HttpsInstaller : public jig::FirmwareInstaller {
 public:
  // Runs on a task of its own, created for the install and gone afterwards, so the big stack is not held all day.
  jig::InstallResult install(const jig::ReleaseInfo& release, const std::function<void(uint32_t, uint32_t)>& progress) override {
    struct Job {
      HttpsInstaller* self;
      const jig::ReleaseInfo* release;
      const std::function<void(uint32_t, uint32_t)>* progress;
      jig::InstallResult result;
      TaskHandle_t waiter;
    } job{this, &release, &progress, {}, xTaskGetCurrentTaskHandle()};
    const BaseType_t created = xTaskCreatePinnedToCore(
        [](void* raw) {
          Job* j = static_cast<Job*>(raw);
          j->result = j->self->run(*j->release, *j->progress);
          xTaskNotifyGive(j->waiter);
          vTaskDelete(nullptr);
        },
        "install", kInstallStack, &job, 1, nullptr, 0);
    if (created != pdPASS) return fail(job.result, "not enough free memory to start the install; try again in a moment");
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    return job.result;
  }

 private:
  jig::InstallResult run(const jig::ReleaseInfo& release, const std::function<void(uint32_t, uint32_t)>& progress) {
    jig::InstallResult out;
    diagf("Update: installing %s (%u bytes), heap %u", release.version.c_str(), static_cast<unsigned>(release.binSize), static_cast<unsigned>(ESP.getFreeHeap()));
    std::string expected = release.sha256;  // from the release information: no extra connections
    if (expected.empty() && !release.sha256Url.empty()) {
      const std::string err = fetchChecksum(release.sha256Url, expected);
      if (!err.empty()) return fail(out, err);
      diagf("Update: checksum file read");
    }
    diagf("Update: checksum %s", expected.empty() ? "not available, the image is only checked by the device" : "known");

    Connection c;
    int code = 0;
    std::string err = getFollowing(c, release.binUrl, code);
    if (!err.empty()) return fail(out, err);
    if (code != 200) return fail(out, failure(code));
    const int length = c.http->getSize();
    diagf("Update: download answered 200, %d bytes", length);
    if (length <= 0) return fail(out, "the server did not say how big the firmware is");
    if (Update.isRunning()) Update.abort();
    if (!Update.begin(static_cast<size_t>(length))) return fail(out, std::string("the firmware does not fit: ") + Update.errorString());

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);
    WiFiClient* stream = c.http->getStreamPtr();
    uint8_t buf[1024];
    size_t written = 0;
    uint32_t lastData = millis(), lastLog = 0, started = millis();
    std::string stop;
    while (written < static_cast<size_t>(length)) {
      const int avail = stream->available();
      if (avail <= 0) {
        if (!stream->connected()) {
          stop = "the connection closed";
          break;
        }
        if (millis() - lastData > kStallMs) {
          stop = "nothing arrived for " + std::to_string(kStallMs / 1000) + " seconds";
          break;
        }
        delay(5);
        continue;
      }
      const int n = stream->readBytes(buf, std::min<size_t>({sizeof buf, static_cast<size_t>(avail), static_cast<size_t>(length) - written}));
      if (n <= 0) continue;
      lastData = millis();
      mbedtls_sha256_update_ret(&sha, buf, static_cast<size_t>(n));
      if (Update.write(buf, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
        stop = std::string("writing the firmware failed: ") + Update.errorString();
        break;
      }
      written += static_cast<size_t>(n);
      progress(static_cast<uint32_t>(written), static_cast<uint32_t>(length));
      if (written - lastLog >= 131072) {
        lastLog = written;
        diagf("Update: %u of %d bytes (%u s, heap %u)", static_cast<unsigned>(written), length, static_cast<unsigned>((millis() - started) / 1000),
              static_cast<unsigned>(ESP.getFreeHeap()));
      }
    }
    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&sha, digest);
    mbedtls_sha256_free(&sha);
    diagf("Update: download ended after %u of %d bytes in %u s%s%s", static_cast<unsigned>(written), length, static_cast<unsigned>((millis() - started) / 1000),
          stop.empty() ? "" : ": ", stop.c_str());
    c.close();

    if (!stop.empty() || written != static_cast<size_t>(length)) {
      Update.abort();
      return fail(out, stop.empty() ? "the download was cut short; nothing was installed" : stop + "; nothing was installed");
    }
    if (!expected.empty() && hex(digest) != expected) {
      Update.abort();
      return fail(out, "the downloaded firmware does not match its checksum; nothing was installed");
    }
    diagf("Update: finishing the image (stack left %u of %u, heap %u)", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
          static_cast<unsigned>(kInstallStack), static_cast<unsigned>(ESP.getFreeHeap()));
    if (!Update.end(true)) return fail(out, std::string("the firmware was refused: ") + Update.errorString());
    diagf("Update: image written%s, restarting (stack left %u)", expected.empty() ? "" : " and checksum ok", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    out.ok = true;
    return out;
  }

  static jig::InstallResult fail(jig::InstallResult& out, const std::string& message) {
    diagf("Update failed: %s", message.c_str());
    out.ok = false;
    out.error = message;
    return out;
  }

  static std::string hex(const uint8_t* d) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 32; ++i) {
      s += digits[d[i] >> 4];
      s += digits[d[i] & 15];
    }
    return s;
  }

  // The checksum file holds the hash first ("<64 hex>  jiggler-esp32.bin", as sha256sum prints it).
  static std::string fetchChecksum(const std::string& url, std::string& hashOut) {
    Connection c;
    int code = 0;
    const std::string err = getFollowing(c, url, code);
    if (!err.empty()) return err;
    if (code != 200) return "the checksum file could not be fetched: " + failure(code);
    String body = c.http->getString();
    c.close();
    body.trim();
    if (body.length() < 64) return "the checksum file is not valid";
    hashOut = body.substring(0, 64).c_str();
    for (char& ch : hashOut) {
      ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
      if (!isxdigit(static_cast<unsigned char>(ch))) return "the checksum file is not valid";
    }
    return "";
  }
};

struct TaskArgs {
  jig::UpdateManager* manager;
  void (*restart)();
};

void taskLoop(void* raw) {
  TaskArgs args = *static_cast<TaskArgs*>(raw);
  GithubSource source;
  HttpsInstaller installer;
  for (;;) {
    args.manager->tick();
    if (args.manager->runPending(source, installer)) {
      vTaskDelay(pdMS_TO_TICKS(1500));  // let the status reach the dashboard
      args.restart();
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

}  // namespace

void startUpdateTask(jig::UpdateManager& manager, void (*restart)()) {
  static TaskArgs args;
  args = {&manager, restart};
  xTaskCreatePinnedToCore(taskLoop, "update", 10240, &args, 1, nullptr, 0);
}
