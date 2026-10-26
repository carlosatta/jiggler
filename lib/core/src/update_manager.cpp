#include "update_manager.h"

#include <ArduinoJson.h>

#include <cctype>

namespace jig {

namespace {

constexpr const char* kBlobKey = "updates";

CmdResult fail(const char* error, const std::string& message, const std::string& field = "") {
  CmdResult r;
  r.error = error;
  r.message = message;
  r.field = field;
  return r;
}

CmdResult ok() {
  CmdResult r;
  r.ok = true;
  return r;
}

bool validName(const std::string& s) {
  if (s.empty() || s.size() > 100 || s == "." || s == "..") return false;
  for (unsigned char c : s)
    if (!(std::isalnum(c) || c == '-' || c == '_' || c == '.')) return false;
  return s.front() != '.';
}

bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string messageFor(const std::string& code) {
  if (code == "bad_release") return "the release information could not be read";
  if (code == "not_a_release") return "the latest release is a draft or a pre-release";
  if (code == "bad_version") return "the latest release has a tag that is not a version like v1.2.3";
  if (code == "no_firmware") return std::string("the latest release has no firmware file (") + kFirmwareAssetName + ")";
  return code;
}

}  // namespace

const char* updateModeName(UpdateMode mode) {
  switch (mode) {
    case UpdateMode::Manual: return "manual";
    case UpdateMode::Automatic: return "automatic";
    case UpdateMode::Ignore: return "ignore";
  }
  return "manual";
}

std::optional<UpdateMode> parseUpdateMode(const std::string& name) {
  if (name == "manual") return UpdateMode::Manual;
  if (name == "automatic") return UpdateMode::Automatic;
  if (name == "ignore") return UpdateMode::Ignore;
  return std::nullopt;
}

const char* updateStateName(UpdateState state) {
  switch (state) {
    case UpdateState::Idle: return "idle";
    case UpdateState::Checking: return "checking";
    case UpdateState::Installing: return "installing";
    case UpdateState::Restarting: return "restarting";
  }
  return "idle";
}

std::optional<Version> parseVersion(const std::string& text) {
  size_t pos = (!text.empty() && (text[0] == 'v' || text[0] == 'V')) ? 1 : 0;
  int parts[3] = {0, 0, 0};
  for (int i = 0; i < 3; ++i) {
    size_t start = pos;
    long value = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
      value = value * 10 + (text[pos] - '0');
      if (value > 99999) return std::nullopt;
      ++pos;
    }
    if (pos == start) return std::nullopt;
    parts[i] = static_cast<int>(value);
    if (i < 2) {
      if (pos >= text.size() || text[pos] != '.') return std::nullopt;
      ++pos;
    }
  }
  if (pos != text.size()) return std::nullopt;  // "-rc1", "+build", a fourth number...
  return Version{parts[0], parts[1], parts[2]};
}

int compareVersions(const Version& a, const Version& b) {
  if (a.major != b.major) return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
  return 0;
}

std::string versionToString(const Version& v) { return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch); }

std::optional<RepoRef> parseRepoUrl(const std::string& url) {
  const std::string prefix = "https://github.com/";
  if (url.compare(0, prefix.size(), prefix) != 0) return std::nullopt;
  std::string rest = url.substr(prefix.size());
  while (!rest.empty() && rest.back() == '/') rest.pop_back();
  if (endsWith(rest, ".git")) rest.resize(rest.size() - 4);
  const size_t slash = rest.find('/');
  if (slash == std::string::npos) return std::nullopt;
  RepoRef ref{rest.substr(0, slash), rest.substr(slash + 1)};
  if (!validName(ref.owner) || !validName(ref.repo)) return std::nullopt;  // also refuses extra path segments
  return ref;
}

std::string repoPageUrl(const RepoRef& repo) { return "https://github.com/" + repo.owner + "/" + repo.repo; }

std::string releasesApiUrl(const RepoRef& repo) { return "https://api.github.com/repos/" + repo.owner + "/" + repo.repo + "/releases/latest"; }

std::string parseRelease(const std::string& json, ReleaseInfo& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json) || !doc.is<JsonObject>()) return "bad_release";
  if (!doc["tag_name"].is<const char*>()) return "bad_release";
  if (doc["draft"].as<bool>() || doc["prerelease"].as<bool>()) return "not_a_release";
  const std::string tag = doc["tag_name"].as<std::string>();
  const auto version = parseVersion(tag);
  if (!version) return "bad_version";

  ReleaseInfo info;
  info.tag = tag;
  info.version = versionToString(*version);
  if (doc["html_url"].is<const char*>()) info.pageUrl = doc["html_url"].as<std::string>();

  std::string binName;
  for (JsonObjectConst asset : doc["assets"].as<JsonArrayConst>()) {
    const std::string name = asset["name"] | "";
    const std::string url = asset["browser_download_url"] | "";
    if (url.compare(0, 8, "https://") != 0) continue;
    if (name == kFirmwareAssetName || (binName != kFirmwareAssetName && endsWith(name, ".bin") && binName.empty())) {
      binName = name;
      info.binUrl = url;
      info.binSize = asset["size"] | 0u;
    }
  }
  if (info.binUrl.empty()) return "no_firmware";
  for (JsonObjectConst asset : doc["assets"].as<JsonArrayConst>()) {
    const std::string url = asset["browser_download_url"] | "";
    if (std::string(asset["name"] | "") == binName + ".sha256" && url.compare(0, 8, "https://") == 0) info.sha256Url = url;
  }
  out = info;
  return "";
}

UpdateManager::UpdateManager(Platform& platform, BlobStore& blobs, std::string currentVersion)
    : platform_(platform), blobs_(blobs), current_(std::move(currentVersion)) {}

void UpdateManager::begin() {
  std::lock_guard<std::mutex> lock(mutex_);
  settings_ = UpdateSettings{};
  blockedVersion_.clear();
  const std::string raw = blobs_.get(kBlobKey);
  JsonDocument doc;
  if (!raw.empty() && !deserializeJson(doc, raw) && doc.is<JsonObject>()) {
    if (doc["mode"].is<const char*>()) {
      if (const auto m = parseUpdateMode(doc["mode"].as<std::string>())) settings_.mode = *m;
    }
    if (doc["repoUrl"].is<const char*>()) {
      if (const auto r = parseRepoUrl(doc["repoUrl"].as<std::string>())) settings_.repoUrl = repoPageUrl(*r);
    }
    const std::string attempted = doc["attempted"] | "";
    if (!attempted.empty() && attempted != current_) blockedVersion_ = attempted;  // installed, but it is not what runs
  }
  saveLocked();  // forgets an attempt that worked (attempted == running version)
  nextCheckAtMs_ = platform_.nowMs() + kFirstUpdateCheckDelayMs;
}

bool UpdateManager::saveLocked() {
  JsonDocument doc;
  doc["mode"] = updateModeName(settings_.mode);
  doc["repoUrl"] = settings_.repoUrl;
  doc["attempted"] = blockedVersion_;
  std::string out;
  serializeJson(doc, out);
  return blobs_.put(kBlobKey, out);
}

bool UpdateManager::availableLocked() const {
  if (!latest_) return false;
  const auto mine = parseVersion(current_);
  const auto theirs = parseVersion(latest_->version);
  return mine && theirs && compareVersions(*theirs, *mine) > 0;
}

UpdateStatus UpdateManager::status() {
  std::lock_guard<std::mutex> lock(mutex_);
  UpdateStatus s;
  s.settings = settings_;
  s.current = current_;
  s.state = state_;
  if (checkedAtMs_) s.checkedAgoMs = platform_.nowMs() - *checkedAtMs_;
  s.latest = latest_;
  s.available = availableLocked();
  s.blocked = s.available && latest_->version == blockedVersion_;
  s.progress = progress_;
  s.error = error_;
  return s;
}

std::string UpdateManager::statusJson() {
  const UpdateStatus s = status();
  JsonDocument doc;
  doc["mode"] = updateModeName(s.settings.mode);
  doc["repoUrl"] = s.settings.repoUrl;
  doc["defaultRepoUrl"] = kDefaultRepoUrl;
  doc["current"] = s.current;
  doc["state"] = updateStateName(s.state);
  doc["progress"] = s.progress;
  if (s.checkedAgoMs) doc["checkedAgoMs"] = *s.checkedAgoMs;
  else doc["checkedAgoMs"] = nullptr;
  if (s.latest) {
    JsonObject l = doc["latest"].to<JsonObject>();
    l["version"] = s.latest->version;
    l["tag"] = s.latest->tag;
    l["pageUrl"] = s.latest->pageUrl;
    l["sizeBytes"] = s.latest->binSize;
  } else {
    doc["latest"] = nullptr;
  }
  doc["available"] = s.available;
  doc["blocked"] = s.blocked;
  if (s.error.empty()) doc["error"] = nullptr;
  else doc["error"] = s.error;
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string UpdateManager::summaryJson() {
  const UpdateStatus s = status();
  JsonDocument doc;
  doc["mode"] = updateModeName(s.settings.mode);
  doc["available"] = s.available;
  if (s.available) doc["version"] = s.latest->version;
  else doc["version"] = nullptr;
  doc["state"] = updateStateName(s.state);
  std::string out;
  serializeJson(doc, out);
  return out;
}

CmdResult UpdateManager::updateSettings(std::optional<UpdateMode> mode, std::optional<std::string> repoUrl) {
  std::string normalized;
  if (repoUrl) {
    if (repoUrl->empty()) normalized = kDefaultRepoUrl;
    else if (const auto ref = parseRepoUrl(*repoUrl)) normalized = repoPageUrl(*ref);
    else return fail("invalid_repo_url", "must be a GitHub repository address like https://github.com/owner/repo", "repoUrl");
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const UpdateSettings before = settings_;
  if (mode) settings_.mode = *mode;
  if (repoUrl) settings_.repoUrl = normalized;
  if (!saveLocked()) {
    settings_ = before;
    return fail("storage_error", "could not save the settings (storage full?)");
  }
  if (settings_.repoUrl != before.repoUrl) {  // what we knew was about another repository
    latest_.reset();
    checkedAtMs_.reset();
    error_.clear();
  }
  if (before.mode == UpdateMode::Ignore && settings_.mode != UpdateMode::Ignore) nextCheckAtMs_ = platform_.nowMs() + 60000;
  return ok();
}

CmdResult UpdateManager::requestCheck() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (busyLocked()) return fail("busy", "an update check or install is already running");
  if (platform_.unixSeconds() == 0) return fail("no_clock", "the device clock is not set: set it in System → Time first (secure connections need the date)");
  checkRequested_ = true;
  return ok();
}

CmdResult UpdateManager::requestInstall() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (busyLocked()) return fail("busy", "an update check or install is already running");
  if (platform_.unixSeconds() == 0) return fail("no_clock", "the device clock is not set: set it in System → Time first (secure connections need the date)");
  if (!latest_ || !availableLocked()) return fail("no_update", "there is no newer version to install: check for updates first");
  installRequested_ = true;
  return ok();
}

void UpdateManager::tick() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (settings_.mode == UpdateMode::Ignore || busyLocked()) return;
  if (!platform_.wifiConnected() || platform_.unixSeconds() == 0) return;
  if (static_cast<int32_t>(platform_.nowMs() - nextCheckAtMs_) < 0) return;
  nextCheckAtMs_ = platform_.nowMs() + kUpdateRetryIntervalMs;  // replaced by the real interval when the check ends
  checkRequested_ = true;
}

bool UpdateManager::runPending(ReleaseSource& source, FirmwareInstaller& installer) {
  enum { None, Check, Install } job = None;
  std::string apiUrl;
  ReleaseInfo release;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (installRequested_) {
      installRequested_ = false;
      checkRequested_ = false;
      if (latest_ && availableLocked()) {
        job = Install;
        release = *latest_;
        state_ = UpdateState::Installing;
        progress_ = 0;
        error_.clear();
        attemptedThisBoot_ = release.version;
        blockedVersion_ = release.version;  // remembered across the restart until the device is seen running it
        saveLocked();
      }
    } else if (checkRequested_) {
      checkRequested_ = false;
      const auto ref = parseRepoUrl(settings_.repoUrl);
      if (ref) {
        job = Check;
        apiUrl = releasesApiUrl(*ref);
        state_ = UpdateState::Checking;
      }
    }
  }
  if (job == None) return false;

  if (job == Check) {
    const FetchResult fetched = source.fetchLatest(apiUrl);
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = UpdateState::Idle;
    if (!fetched.ok) {
      error_ = fetched.error;
      nextCheckAtMs_ = platform_.nowMs() + kUpdateRetryIntervalMs;
      return false;
    }
    ReleaseInfo info;
    const std::string code = parseRelease(fetched.body, info);
    if (!code.empty()) {
      error_ = messageFor(code);
      latest_.reset();
      checkedAtMs_ = platform_.nowMs();
      nextCheckAtMs_ = platform_.nowMs() + kUpdateCheckIntervalMs;
      return false;
    }
    latest_ = info;
    checkedAtMs_ = platform_.nowMs();
    error_.clear();
    nextCheckAtMs_ = platform_.nowMs() + kUpdateCheckIntervalMs;
    if (settings_.mode == UpdateMode::Automatic && availableLocked() && info.version != blockedVersion_ && info.version != attemptedThisBoot_)
      installRequested_ = true;
    return false;
  }

  if (installActive_) installActive_(true);
  const InstallResult result = installer.install(release, [this](uint32_t done, uint32_t total) {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_ = total ? static_cast<uint8_t>(static_cast<uint64_t>(done) * 100 / total) : 0;
  });
  std::lock_guard<std::mutex> lock(mutex_);
  if (result.ok) {
    state_ = UpdateState::Restarting;
    progress_ = 100;
    return true;
  }
  state_ = UpdateState::Idle;
  error_ = result.error;
  blockedVersion_.clear();  // nothing was installed: it is not "tried and failed to take"
  saveLocked();
  nextCheckAtMs_ = platform_.nowMs() + kUpdateRetryIntervalMs;
  if (installActive_) installActive_(false);
  return false;
}

}  // namespace jig
