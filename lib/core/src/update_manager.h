#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include "controller.h"
#include "platform.h"

namespace jig {

constexpr const char* kDefaultRepoUrl = "https://github.com/carlosatta/jiggler";
constexpr const char* kFirmwareAssetName = "jiggler-esp32.bin";  // what a release must contain
constexpr uint32_t kFirstUpdateCheckDelayMs = 10u * 60u * 1000u;  // let WiFi and the clock settle after boot
constexpr uint32_t kUpdateCheckIntervalMs = 24u * 60u * 60u * 1000u;
constexpr uint32_t kUpdateRetryIntervalMs = 60u * 60u * 1000u;

// Manual: the device looks for new releases and tells you; you press Install. Automatic: it also installs them.
// Ignore: it never looks by itself (Check now still works).
enum class UpdateMode { Manual, Automatic, Ignore };
const char* updateModeName(UpdateMode mode);
std::optional<UpdateMode> parseUpdateMode(const std::string& name);

struct Version {
  int major = 0, minor = 0, patch = 0;
};
std::optional<Version> parseVersion(const std::string& text);  // "1.2.3" or "v1.2.3"; anything else (suffixes, missing parts) is refused
int compareVersions(const Version& a, const Version& b);       // <0, 0, >0
std::string versionToString(const Version& v);

struct RepoRef {
  std::string owner, repo;
};
// "https://github.com/owner/repo", with an optional ".git" and trailing slash. Only github.com over https.
std::optional<RepoRef> parseRepoUrl(const std::string& url);
std::string repoPageUrl(const RepoRef& repo);    // https://github.com/owner/repo
std::string releasesApiUrl(const RepoRef& repo); // https://api.github.com/repos/owner/repo/releases/latest

struct ReleaseInfo {
  std::string tag;         // "v0.7.0"
  std::string version;     // "0.7.0"
  std::string pageUrl;     // the release page
  std::string binUrl;      // the firmware asset
  uint32_t binSize = 0;
  std::string sha256Url;   // optional "<asset>.sha256"
};
// The GitHub "latest release" JSON (or a trimmed copy of it). Returns "" on success, else an error code:
// bad_release, not_a_release (draft or prerelease), bad_version, no_firmware.
std::string parseRelease(const std::string& json, ReleaseInfo& out);

struct FetchResult {
  bool ok = false;
  std::string body;   // the release JSON
  std::string error;  // a sentence for the owner when !ok
};
struct ReleaseSource {  // blocking; runs on the worker
  virtual ~ReleaseSource() = default;
  virtual FetchResult fetchLatest(const std::string& apiUrl) = 0;
};

struct InstallResult {
  bool ok = false;
  std::string error;
};
struct FirmwareInstaller {  // blocking; downloads, verifies and writes the new image
  virtual ~FirmwareInstaller() = default;
  virtual InstallResult install(const ReleaseInfo& release, const std::function<void(uint32_t done, uint32_t total)>& progress) = 0;
};

struct UpdateSettings {
  UpdateMode mode = UpdateMode::Manual;
  std::string repoUrl = kDefaultRepoUrl;
};

enum class UpdateState { Idle, Checking, Installing, Restarting };
const char* updateStateName(UpdateState state);

struct UpdateStatus {
  UpdateSettings settings;
  std::string current;
  UpdateState state = UpdateState::Idle;
  std::optional<uint32_t> checkedAgoMs;
  std::optional<ReleaseInfo> latest;
  bool available = false;  // latest is newer than what runs
  bool blocked = false;    // that version was installed before and did not take effect: no automatic retry
  uint8_t progress = 0;    // 0..100 while installing
  std::string error;       // the last check or install failed
};

// Looks for new releases of the project, tells the owner, and installs them when asked or when set to automatic.
// The network work is done by a worker that calls runPending(); everything else here only records and decides.
// Thread-safe.
class UpdateManager {
 public:
  UpdateManager(Platform& platform, BlobStore& blobs, std::string currentVersion);

  void begin();
  UpdateStatus status();
  std::string statusJson();   // GET /api/updates
  std::string summaryJson();  // the small object in /api/status
  std::string defaultRepoUrl() const { return kDefaultRepoUrl; }

  CmdResult updateSettings(std::optional<UpdateMode> mode, std::optional<std::string> repoUrl);  // invalid_repo_url, storage_error
  CmdResult requestCheck();    // errors: busy, no_clock
  CmdResult requestInstall();  // errors: busy, no_clock, no_update
  void tick();                 // the automatic checks; about once a second

  // Worker side: does a requested check or install. True once an image was written and the device should restart.
  bool runPending(ReleaseSource& source, FirmwareInstaller& installer);
  void setInstallActive(std::function<void(bool)> hook) { installActive_ = std::move(hook); }  // e.g. keep Bluetooth quiet

 private:
  bool busyLocked() const { return state_ != UpdateState::Idle || checkRequested_ || installRequested_; }
  bool availableLocked() const;
  bool saveLocked();

  Platform& platform_;
  BlobStore& blobs_;
  const std::string current_;
  std::function<void(bool)> installActive_;
  std::mutex mutex_;
  UpdateSettings settings_;
  UpdateState state_ = UpdateState::Idle;
  bool checkRequested_ = false, installRequested_ = false;
  uint32_t nextCheckAtMs_ = 0;
  std::optional<uint32_t> checkedAtMs_;
  std::optional<ReleaseInfo> latest_;
  uint8_t progress_ = 0;
  std::string error_;
  std::string blockedVersion_;    // installed in an earlier boot, yet the device still runs something else
  std::string attemptedThisBoot_; // do not retry automatically after a failure
};

}  // namespace jig
