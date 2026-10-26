#include "update_task.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

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
    diagf("Update: GitHub answered %d (stack left %u)", code, static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) * 4));
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

class HttpsInstaller : public jig::FirmwareInstaller {
 public:
  jig::InstallResult install(const jig::ReleaseInfo& release, const std::function<void(uint32_t, uint32_t)>& progress) override {
    jig::InstallResult out;
    std::string expected;
    if (!release.sha256Url.empty()) {
      out.error = fetchChecksum(release.sha256Url, expected);
      if (!out.error.empty()) return out;
    }

    WiFiClientSecure client;
    HTTPClient http;
    out.error = open(http, client, release.binUrl, true);
    if (!out.error.empty()) return out;
    const int code = http.GET();
    if (code != 200) {
      out.error = failure(code);
      http.end();
      return out;
    }
    const int length = http.getSize();
    if (length <= 0) {
      out.error = "the server did not say how big the firmware is";
      http.end();
      return out;
    }
    if (Update.isRunning()) Update.abort();
    if (!Update.begin(static_cast<size_t>(length))) {
      out.error = std::string("the firmware does not fit: ") + Update.errorString();
      http.end();
      return out;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    size_t written = 0;
    uint32_t lastData = millis(), lastLog = 0;
    while (written < static_cast<size_t>(length)) {
      const int avail = stream->available();
      if (avail <= 0) {
        if (!stream->connected() && stream->available() <= 0) break;
        if (millis() - lastData > kStallMs) break;
        delay(5);
        continue;
      }
      const int n = stream->readBytes(buf, std::min<size_t>({sizeof buf, static_cast<size_t>(avail), static_cast<size_t>(length) - written}));
      if (n <= 0) continue;
      lastData = millis();
      mbedtls_sha256_update_ret(&sha, buf, static_cast<size_t>(n));
      if (Update.write(buf, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
        out.error = std::string("writing the firmware failed: ") + Update.errorString();
        break;
      }
      written += static_cast<size_t>(n);
      progress(static_cast<uint32_t>(written), static_cast<uint32_t>(length));
      if (written - lastLog >= 262144) {
        lastLog = written;
        diagf("Update: %u of %d bytes", static_cast<unsigned>(written), length);
      }
    }
    http.end();
    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&sha, digest);
    mbedtls_sha256_free(&sha);

    if (!out.error.empty()) {
      Update.abort();
      return out;
    }
    if (written != static_cast<size_t>(length)) {
      Update.abort();
      out.error = "the download was cut short; nothing was installed";
      return out;
    }
    if (!expected.empty() && hex(digest) != expected) {
      Update.abort();
      out.error = "the downloaded firmware does not match its checksum; nothing was installed";
      return out;
    }
    if (!Update.end(true)) {
      out.error = std::string("the firmware was refused: ") + Update.errorString();
      return out;
    }
    diagf("Update: %u bytes written%s, restarting", static_cast<unsigned>(written), expected.empty() ? "" : ", checksum ok");
    out.ok = true;
    return out;
  }

 private:
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
    WiFiClientSecure client;
    HTTPClient http;
    std::string err = open(http, client, url, true);
    if (!err.empty()) return err;
    const int code = http.GET();
    if (code != 200) {
      http.end();
      return "the checksum file could not be fetched: " + failure(code);
    }
    String body = http.getString();
    http.end();
    body.trim();
    if (body.length() < 64) return "the checksum file is not valid";
    hashOut = body.substring(0, 64).c_str();
    for (char& c : hashOut) {
      c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
      if (!isxdigit(static_cast<unsigned char>(c))) return "the checksum file is not valid";
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
  xTaskCreatePinnedToCore(taskLoop, "update", 12288, &args, 1, nullptr, 0);
}
