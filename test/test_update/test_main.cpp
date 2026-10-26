#include <unity.h>

#include <ArduinoJson.h>

#include "api_ex.h"
#include "fakes.h"
#include "update_manager.h"

using namespace jig;

void setUp() {}
void tearDown() {}

namespace {

bool parse_is_null(const std::string& body) {
  JsonDocument doc;
  deserializeJson(doc, body);
  return doc["update"].isNull();
}

const char* kRelease =
    "{\"tag_name\":\"v0.7.0\",\"draft\":false,\"prerelease\":false,\"html_url\":\"https://github.com/carlosatta/jiggler/releases/tag/v0.7.0\","
    "\"assets\":[{\"name\":\"jiggler-esp32.bin.sha256\",\"browser_download_url\":\"https://github.com/x/y/releases/download/v0.7.0/jiggler-esp32.bin.sha256\",\"size\":90},"
    "{\"name\":\"jiggler-esp32.bin\",\"browser_download_url\":\"https://github.com/x/y/releases/download/v0.7.0/jiggler-esp32.bin\",\"size\":1500000}]}";

struct FakeSource : ReleaseSource {
  FetchResult next;
  std::vector<std::string> urls;
  FetchResult fetchLatest(const std::string& apiUrl) override {
    urls.push_back(apiUrl);
    return next;
  }
};

struct FakeInstaller : FirmwareInstaller {
  InstallResult result{true, ""};
  int calls = 0;
  std::string lastVersion;
  InstallResult install(const ReleaseInfo& release, const std::function<void(uint32_t, uint32_t)>& progress) override {
    calls++;
    lastVersion = release.version;
    progress(500, 1000);
    progress(1000, 1000);
    return result;
  }
};

struct Env {
  FakeClock clock;
  FlakyBlobStore blobs;
  ClockStore clockStore{clock, blobs};
  FakeSource source;
  FakeInstaller installer;
  UpdateManager manager;
  std::vector<bool> activeCalls;
  explicit Env(const char* current = "0.6.0") : manager(clock, blobs, clockStore, current) {
    clockStore.begin();
    clock.local = LocalTime{0, 12 * 60, 20003};  // noon, Monday
    manager.setInstallActive([this](bool on) { activeCalls.push_back(on); });
    manager.begin();
    source.next = {true, kRelease, ""};
  }
  void check() {
    TEST_ASSERT_TRUE(manager.requestCheck().ok);
    manager.runPending(source, installer);
  }
};

}  // namespace

void test_versions_parse_and_compare() {
  TEST_ASSERT_TRUE(parseVersion("1.2.3").has_value());
  TEST_ASSERT_TRUE(parseVersion("v0.6.0").has_value());
  TEST_ASSERT_EQUAL_STRING("0.6.0", versionToString(*parseVersion("V0.6.0")).c_str());
  for (const char* bad : {"", "v", "1", "1.2", "1.2.3.4", "1.2.x", "v1.2.3-rc1", "1.2.3+b", "latest", "..", "1..3", "100000.0.0"})
    TEST_ASSERT_FALSE_MESSAGE(parseVersion(bad).has_value(), bad);
  auto v = [](const char* s) { return *parseVersion(s); };
  TEST_ASSERT_TRUE(compareVersions(v("0.6.1"), v("0.6.0")) > 0);
  TEST_ASSERT_TRUE(compareVersions(v("0.10.0"), v("0.9.9")) > 0);  // numbers, not text
  TEST_ASSERT_TRUE(compareVersions(v("1.0.0"), v("0.99.99")) > 0);
  TEST_ASSERT_TRUE(compareVersions(v("0.6.0"), v("v0.6.0")) == 0);
  TEST_ASSERT_TRUE(compareVersions(v("0.5.9"), v("0.6.0")) < 0);
}

void test_repository_addresses() {
  for (const char* ok : {"https://github.com/carlosatta/jiggler", "https://github.com/carlosatta/jiggler/", "https://github.com/carlosatta/jiggler.git",
                         "https://github.com/some-org/my_repo.v2"}) {
    auto r = parseRepoUrl(ok);
    TEST_ASSERT_TRUE_MESSAGE(r.has_value(), ok);
  }
  auto r = parseRepoUrl("https://github.com/carlosatta/jiggler.git/");
  TEST_ASSERT_EQUAL_STRING("carlosatta", r->owner.c_str());
  TEST_ASSERT_EQUAL_STRING("jiggler", r->repo.c_str());
  TEST_ASSERT_EQUAL_STRING("https://github.com/carlosatta/jiggler", repoPageUrl(*r).c_str());
  TEST_ASSERT_EQUAL_STRING("https://api.github.com/repos/carlosatta/jiggler/releases/latest", releasesApiUrl(*r).c_str());
  for (const char* bad : {"", "github.com/a/b", "http://github.com/a/b", "https://gitlab.com/a/b", "https://github.com/a", "https://github.com/a/b/c",
                          "https://github.com/a/b/releases", "https://github.com//b", "https://github.com/a b/c", "https://github.com/a/..",
                          "https://github.com/.a/b", "https://github.com.evil.com/a/b", "https://github.com/a/b?x=1", "https://github.com/a/b#x"})
    TEST_ASSERT_FALSE_MESSAGE(parseRepoUrl(bad).has_value(), bad);
}

void test_a_release_is_read_for_its_version_and_firmware() {
  ReleaseInfo info;
  TEST_ASSERT_EQUAL_STRING("", parseRelease(kRelease, info).c_str());
  TEST_ASSERT_EQUAL_STRING("v0.7.0", info.tag.c_str());
  TEST_ASSERT_EQUAL_STRING("0.7.0", info.version.c_str());
  TEST_ASSERT_EQUAL_UINT32(1500000, info.binSize);
  TEST_ASSERT_TRUE(info.binUrl.find("jiggler-esp32.bin") != std::string::npos);
  TEST_ASSERT_TRUE(info.sha256Url.find(".sha256") != std::string::npos);
  TEST_ASSERT_TRUE(info.pageUrl.find("releases/tag") != std::string::npos);
}

void test_release_problems_have_their_own_codes() {
  ReleaseInfo info;
  TEST_ASSERT_EQUAL_STRING("bad_release", parseRelease("nope", info).c_str());
  TEST_ASSERT_EQUAL_STRING("bad_release", parseRelease("{}", info).c_str());
  TEST_ASSERT_EQUAL_STRING("not_a_release", parseRelease("{\"tag_name\":\"v1.0.0\",\"prerelease\":true,\"assets\":[]}", info).c_str());
  TEST_ASSERT_EQUAL_STRING("not_a_release", parseRelease("{\"tag_name\":\"v1.0.0\",\"draft\":true,\"assets\":[]}", info).c_str());
  TEST_ASSERT_EQUAL_STRING("bad_version", parseRelease("{\"tag_name\":\"nightly\",\"assets\":[]}", info).c_str());
  TEST_ASSERT_EQUAL_STRING("no_firmware", parseRelease("{\"tag_name\":\"v1.0.0\",\"assets\":[]}", info).c_str());
  TEST_ASSERT_EQUAL_STRING("no_firmware", parseRelease("{\"tag_name\":\"v1.0.0\"}", info).c_str());
  TEST_ASSERT_EQUAL_STRING("no_firmware",
      parseRelease("{\"tag_name\":\"v1.0.0\",\"assets\":[{\"name\":\"jiggler-esp32.bin\",\"browser_download_url\":\"http://insecure.example/x.bin\",\"size\":1}]}", info).c_str());
  TEST_ASSERT_EQUAL_STRING("no_firmware",
      parseRelease("{\"tag_name\":\"v1.0.0\",\"assets\":[{\"name\":\"notes.txt\",\"browser_download_url\":\"https://x/notes.txt\",\"size\":1}]}", info).c_str());
}

void test_the_official_firmware_file_wins_over_other_bin_files() {
  ReleaseInfo info;
  const char* json =
      "{\"tag_name\":\"v1.0.0\",\"assets\":[{\"name\":\"other.bin\",\"browser_download_url\":\"https://x/other.bin\",\"size\":1},"
      "{\"name\":\"jiggler-esp32.bin\",\"browser_download_url\":\"https://x/jiggler-esp32.bin\",\"size\":2}]}";
  TEST_ASSERT_EQUAL_STRING("", parseRelease(json, info).c_str());
  TEST_ASSERT_EQUAL_STRING("https://x/jiggler-esp32.bin", info.binUrl.c_str());
  TEST_ASSERT_TRUE(info.sha256Url.empty());
  const char* only = "{\"tag_name\":\"v1.0.0\",\"assets\":[{\"name\":\"fw.bin\",\"browser_download_url\":\"https://x/fw.bin\",\"size\":3}]}";
  TEST_ASSERT_EQUAL_STRING("", parseRelease(only, info).c_str());
  TEST_ASSERT_EQUAL_STRING("https://x/fw.bin", info.binUrl.c_str());
}

void test_settings_default_persist_and_validate() {
  Env e;
  UpdateStatus s = e.manager.status();
  TEST_ASSERT_TRUE(UpdateMode::Manual == s.settings.mode);
  TEST_ASSERT_EQUAL_STRING(kDefaultRepoUrl, s.settings.repoUrl.c_str());
  TEST_ASSERT_TRUE(e.manager.updateSettings(UpdateMode::Automatic, std::string("https://github.com/someone/fork.git/")).ok);
  UpdateManager again(e.clock, e.blobs, e.clockStore, "0.6.0");
  again.begin();
  TEST_ASSERT_TRUE(UpdateMode::Automatic == again.status().settings.mode);
  TEST_ASSERT_EQUAL_STRING("https://github.com/someone/fork", again.status().settings.repoUrl.c_str());
  CmdResult bad = e.manager.updateSettings(std::nullopt, std::string("https://example.com/a/b"));
  TEST_ASSERT_FALSE(bad.ok);
  TEST_ASSERT_EQUAL_STRING("invalid_repo_url", bad.error.c_str());
  TEST_ASSERT_EQUAL_STRING("repoUrl", bad.field.c_str());
  TEST_ASSERT_TRUE(e.manager.updateSettings(std::nullopt, std::string("")).ok);  // empty = back to the default
  TEST_ASSERT_EQUAL_STRING(kDefaultRepoUrl, e.manager.status().settings.repoUrl.c_str());
  TEST_ASSERT_TRUE(parseUpdateMode("ignore").has_value());
  TEST_ASSERT_FALSE(parseUpdateMode("sometimes").has_value());
  TEST_ASSERT_EQUAL_STRING("automatic", updateModeName(UpdateMode::Automatic));
}

void test_a_full_storage_changes_nothing() {
  Env e;
  e.blobs.failPuts = true;
  CmdResult r = e.manager.updateSettings(UpdateMode::Ignore, std::nullopt);
  TEST_ASSERT_EQUAL_STRING("storage_error", r.error.c_str());
  TEST_ASSERT_TRUE(UpdateMode::Manual == e.manager.status().settings.mode);
}

void test_a_check_finds_a_newer_release() {
  Env e;
  e.check();
  UpdateStatus s = e.manager.status();
  TEST_ASSERT_TRUE(s.available);
  TEST_ASSERT_EQUAL_STRING("0.7.0", s.latest->version.c_str());
  TEST_ASSERT_TRUE(s.checkedAgoMs.has_value());
  TEST_ASSERT_TRUE(s.error.empty());
  TEST_ASSERT_TRUE(UpdateState::Idle == s.state);
  TEST_ASSERT_EQUAL_STRING("https://api.github.com/repos/carlosatta/jiggler/releases/latest", e.source.urls[0].c_str());
  JsonDocument d;
  TEST_ASSERT_TRUE(!deserializeJson(d, e.manager.statusJson()));
  TEST_ASSERT_TRUE(d["available"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("0.7.0", d["latest"]["version"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("0.6.0", d["current"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("manual", d["mode"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING(kDefaultRepoUrl, d["defaultRepoUrl"].as<const char*>());
  TEST_ASSERT_TRUE(d["error"].isNull());
  JsonDocument sum;
  deserializeJson(sum, e.manager.summaryJson());
  TEST_ASSERT_TRUE(sum["available"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("0.7.0", sum["version"].as<const char*>());
}

void test_the_same_or_an_older_release_is_not_an_update() {
  Env same("0.7.0");
  same.check();
  TEST_ASSERT_FALSE(same.manager.status().available);
  TEST_ASSERT_EQUAL_STRING("no_update", same.manager.requestInstall().error.c_str());
  Env newer("0.8.0");
  newer.check();
  TEST_ASSERT_FALSE(newer.manager.status().available);
}

void test_failures_are_reported_in_words() {
  Env e;
  e.source.next = {false, "", "could not reach github.com"};
  e.check();
  TEST_ASSERT_EQUAL_STRING("could not reach github.com", e.manager.status().error.c_str());
  TEST_ASSERT_FALSE(e.manager.status().latest.has_value());
  e.source.next = {true, "{\"tag_name\":\"v1.0.0\",\"assets\":[]}", ""};
  e.check();
  TEST_ASSERT_TRUE(e.manager.status().error.find("jiggler-esp32.bin") != std::string::npos);
  e.source.next = {true, kRelease, ""};
  e.check();
  TEST_ASSERT_TRUE(e.manager.status().error.empty());  // a good check clears the old error
}

void test_requests_are_refused_while_busy_or_without_a_clock() {
  Env e;
  TEST_ASSERT_TRUE(e.manager.requestCheck().ok);
  TEST_ASSERT_EQUAL_STRING("busy", e.manager.requestCheck().error.c_str());
  TEST_ASSERT_EQUAL_STRING("busy", e.manager.requestInstall().error.c_str());
  e.manager.runPending(e.source, e.installer);
  e.clock.unixTime = 0;
  TEST_ASSERT_EQUAL_STRING("no_clock", e.manager.requestCheck().error.c_str());
  TEST_ASSERT_EQUAL_STRING("no_clock", e.manager.requestInstall().error.c_str());
  TEST_ASSERT_FALSE(e.manager.runPending(e.source, e.installer));  // nothing pending: no work
}

void test_an_install_writes_reports_progress_and_asks_for_a_restart() {
  Env e;
  e.check();
  TEST_ASSERT_TRUE(e.manager.requestInstall().ok);
  TEST_ASSERT_TRUE(e.manager.runPending(e.source, e.installer));
  TEST_ASSERT_EQUAL_INT(1, e.installer.calls);
  TEST_ASSERT_EQUAL_STRING("0.7.0", e.installer.lastVersion.c_str());
  TEST_ASSERT_TRUE(UpdateState::Restarting == e.manager.status().state);
  TEST_ASSERT_EQUAL_UINT8(100, e.manager.status().progress);
  TEST_ASSERT_EQUAL_UINT32(1, e.activeCalls.size());   // Bluetooth went quiet, and stays so until the restart
  TEST_ASSERT_TRUE(e.activeCalls[0]);
}

void test_after_the_restart_the_attempt_is_forgotten_when_the_new_version_runs() {
  Env e;
  e.check();
  e.manager.requestInstall();
  e.manager.runPending(e.source, e.installer);
  UpdateManager rebooted(e.clock, e.blobs, e.clockStore, "0.7.0");  // the new firmware started
  rebooted.begin();
  TEST_ASSERT_FALSE(rebooted.status().blocked);
  UpdateManager again(e.clock, e.blobs, e.clockStore, "0.6.0");     // and a later boot of anything else is not blocked either
  again.begin();
  FakeSource src;
  src.next = {true, kRelease, ""};
  again.requestCheck();
  again.runPending(src, e.installer);
  TEST_ASSERT_TRUE(again.status().available);
  TEST_ASSERT_FALSE(again.status().blocked);
}

void test_an_image_that_did_not_take_is_marked_blocked() {
  Env e;
  e.check();
  e.manager.requestInstall();
  e.manager.runPending(e.source, e.installer);
  UpdateManager stillOld(e.clock, e.blobs, e.clockStore, "0.6.0");  // the old firmware is still what runs after the restart
  stillOld.begin();
  FakeSource src;
  src.next = {true, kRelease, ""};
  stillOld.requestCheck();
  stillOld.runPending(src, e.installer);
  TEST_ASSERT_TRUE(stillOld.status().available);
  TEST_ASSERT_TRUE(stillOld.status().blocked);
  stillOld.updateSettings(UpdateMode::Automatic, std::nullopt);
  stillOld.requestCheck();
  TEST_ASSERT_FALSE(stillOld.runPending(src, e.installer));
  TEST_ASSERT_FALSE(stillOld.runPending(src, e.installer));        // automatic mode does not loop on it
  TEST_ASSERT_TRUE(stillOld.requestInstall().ok);                  // but the owner can try again
}

void test_a_failed_install_reports_and_does_not_block() {
  Env e;
  e.check();
  e.installer.result = {false, "the download stopped"};
  e.manager.requestInstall();
  TEST_ASSERT_FALSE(e.manager.runPending(e.source, e.installer));
  UpdateStatus s = e.manager.status();
  TEST_ASSERT_EQUAL_STRING("the download stopped", s.error.c_str());
  TEST_ASSERT_TRUE(UpdateState::Idle == s.state);
  TEST_ASSERT_FALSE(s.blocked);
  TEST_ASSERT_EQUAL_UINT32(2, e.activeCalls.size());
  TEST_ASSERT_TRUE(e.activeCalls[0]);
  TEST_ASSERT_FALSE(e.activeCalls[1]);                 // Bluetooth is back
  UpdateManager rebooted(e.clock, e.blobs, e.clockStore, "0.6.0");
  rebooted.begin();
  e.installer.result = {true, ""};
  FakeSource src;
  src.next = {true, kRelease, ""};
  rebooted.requestCheck();
  rebooted.runPending(src, e.installer);
  TEST_ASSERT_FALSE(rebooted.status().blocked);        // nothing was installed, so it can be tried again
}

void test_manual_mode_never_installs_by_itself() {
  Env e;
  e.manager.updateSettings(UpdateMode::Manual, std::nullopt);
  e.check();
  TEST_ASSERT_FALSE(e.manager.runPending(e.source, e.installer));
  TEST_ASSERT_EQUAL_INT(0, e.installer.calls);
}

void test_automatic_mode_installs_what_it_finds() {
  Env e;
  e.manager.updateSettings(UpdateMode::Automatic, std::nullopt);
  e.check();
  TEST_ASSERT_TRUE(e.manager.runPending(e.source, e.installer));   // the second step: the install it queued
  TEST_ASSERT_EQUAL_INT(1, e.installer.calls);
}

void test_automatic_mode_does_not_retry_a_failed_or_blocked_version() {
  Env e;
  e.manager.updateSettings(UpdateMode::Automatic, std::nullopt);
  e.installer.result = {false, "no space"};
  e.check();
  TEST_ASSERT_FALSE(e.manager.runPending(e.source, e.installer));  // tried once and failed
  TEST_ASSERT_EQUAL_INT(1, e.installer.calls);
  e.check();                                                       // the next check sees the same version again
  TEST_ASSERT_FALSE(e.manager.runPending(e.source, e.installer));
  TEST_ASSERT_EQUAL_INT(1, e.installer.calls);                     // not again until the next boot
  TEST_ASSERT_TRUE(e.manager.requestInstall().ok);                 // the owner can still ask for it
}

LocalTime atDay(uint32_t day, int hour, int minute) { return LocalTime{static_cast<uint8_t>((day + 3) % 7), static_cast<uint16_t>(hour * 60 + minute), day}; }

void test_the_daily_check_waits_for_its_time_the_clock_and_the_wifi() {
  Env e;                                                           // the default time is 03:00
  e.clock.now = kFirstUpdateCheckDelayMs;                          // the boot settle time has passed
  e.clock.local = atDay(20003, 2, 59);
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(0, e.source.urls.size());               // too early in the day
  e.clock.local = atDay(20003, 3, 0);
  e.clock.unixTime = 0;                                                // no clock: secure connections would fail
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(0, e.source.urls.size());
  e.clock.unixTime = 1760000000;
  e.clock.wifi = false;
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(0, e.source.urls.size());
  e.clock.wifi = true;
  e.clock.local.reset();                                           // no local time yet
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(0, e.source.urls.size());
  e.clock.local = atDay(20003, 3, 0);
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(1, e.source.urls.size());               // the look of the day
}

void test_it_looks_once_a_day_not_again_until_the_next_day() {
  Env e;
  e.clock.now = kFirstUpdateCheckDelayMs;
  e.clock.local = atDay(20003, 3, 5);
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(1, e.source.urls.size());
  e.clock.now += 5 * kUpdateRetryIntervalMs;                       // hours later, same day
  e.clock.local = atDay(20003, 15, 0);
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(1, e.source.urls.size());
  e.clock.now += 5 * kUpdateRetryIntervalMs;
  e.clock.local = atDay(20004, 1, 0);                              // the next day, before the time
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(1, e.source.urls.size());
  e.clock.local = atDay(20004, 3, 0);
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(2, e.source.urls.size());
}

void test_a_board_that_was_off_at_the_chosen_time_looks_when_it_is_up_again() {
  Env e;
  e.clock.now = kFirstUpdateCheckDelayMs - 1;
  e.clock.local = atDay(20003, 17, 0);                             // booted in the afternoon
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(0, e.source.urls.size());               // still settling after boot
  e.clock.now += 1;
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(1, e.source.urls.size());               // today's look is overdue, so it happens now
}

void test_the_time_is_a_setting_that_persists_and_changes_when_it_looks() {
  Env e;
  TEST_ASSERT_EQUAL_UINT16(180, e.manager.status().settings.checkMinute);
  TEST_ASSERT_TRUE(e.manager.updateSettings(std::nullopt, std::nullopt, 22 * 60 + 30).ok);
  UpdateManager again(e.clock, e.blobs, e.clockStore, "0.6.0");
  again.begin();
  TEST_ASSERT_EQUAL_UINT16(22 * 60 + 30, again.status().settings.checkMinute);
  JsonDocument d;
  deserializeJson(d, again.statusJson());
  TEST_ASSERT_EQUAL_STRING("22:30", d["checkTime"].as<const char*>());
  CmdResult bad = e.manager.updateSettings(std::nullopt, std::nullopt, 1440);
  TEST_ASSERT_EQUAL_STRING("invalid_time", bad.error.c_str());
  e.clock.now = kFirstUpdateCheckDelayMs;
  e.clock.local = atDay(20003, 21, 0);
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(0, e.source.urls.size());               // 22:30 has not come
  e.clock.local = atDay(20003, 22, 30);
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(1, e.source.urls.size());
}

void test_times_of_day_are_parsed_and_written() {
  TEST_ASSERT_EQUAL_UINT16(0, *parseTimeOfDay("00:00"));
  TEST_ASSERT_EQUAL_UINT16(23 * 60 + 59, *parseTimeOfDay("23:59"));
  for (const char* bad : {"", "3:00", "24:00", "12:60", "12-30", "ab:cd", "12:3", "12:300"}) TEST_ASSERT_FALSE_MESSAGE(parseTimeOfDay(bad).has_value(), bad);
  TEST_ASSERT_EQUAL_STRING("03:05", formatTimeOfDay(185).c_str());
}

void test_a_failed_check_is_retried_within_the_hour() {
  Env e;
  e.clock.now = kFirstUpdateCheckDelayMs;
  e.clock.local = atDay(20003, 4, 0);
  e.source.next = {false, "", "offline"};
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(1, e.source.urls.size());
  e.clock.now += kUpdateRetryIntervalMs - 1;
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(1, e.source.urls.size());
  e.clock.now += 1;
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(2, e.source.urls.size());               // a failed look does not use up the day
}

void test_ignore_mode_never_checks_by_itself_but_check_now_still_works() {
  Env e;
  e.manager.updateSettings(UpdateMode::Ignore, std::nullopt);
  e.clock.now = kFirstUpdateCheckDelayMs * 10;
  e.clock.local = atDay(20003, 5, 0);
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(0, e.source.urls.size());
  e.check();
  TEST_ASSERT_EQUAL_UINT32(1, e.source.urls.size());
  e.clock.local = atDay(20004, 5, 0);                              // the next day: ignored all the same
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(1, e.source.urls.size());
  e.manager.updateSettings(UpdateMode::Manual, std::nullopt);     // turning it back on: a look soon, once the time has come
  e.clock.now += 59999;
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(1, e.source.urls.size());
  e.clock.now += 1;
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_UINT32(2, e.source.urls.size());
}

void test_automatic_mode_installs_at_the_chosen_time() {
  Env e;
  e.manager.updateSettings(UpdateMode::Automatic, std::nullopt, 3 * 60);
  e.clock.now = kFirstUpdateCheckDelayMs;
  e.clock.local = atDay(20003, 2, 0);
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);
  TEST_ASSERT_EQUAL_INT(0, e.installer.calls);                     // not at 02:00
  e.clock.local = atDay(20003, 3, 1);
  e.manager.tick();
  e.manager.runPending(e.source, e.installer);                     // the look, which finds 0.7.0
  TEST_ASSERT_TRUE(e.manager.runPending(e.source, e.installer));   // the install that follows
  TEST_ASSERT_EQUAL_INT(1, e.installer.calls);
}

void test_changing_the_repository_forgets_what_was_found_there() {
  Env e;
  e.check();
  TEST_ASSERT_TRUE(e.manager.status().available);
  e.manager.updateSettings(std::nullopt, std::string("https://github.com/someone/fork"));
  TEST_ASSERT_FALSE(e.manager.status().available);
  TEST_ASSERT_FALSE(e.manager.status().latest.has_value());
  e.check();
  TEST_ASSERT_EQUAL_STRING("https://api.github.com/repos/someone/fork/releases/latest", e.source.urls.back().c_str());
}

void test_update_routes() {
  Env e;
  XorShiftRng rng{1};
  MemStore config;
  Controller controller{e.clock, rng, config};
  ApiContext ctx{controller};
  ApiContext none{controller};
  TEST_ASSERT_EQUAL_INT(404, handleApiEx(none, "GET", "/api/updates", "").status);
  ctx.updates = &e.manager;

  HttpResponse g = handleApiEx(ctx, "GET", "/api/updates", "");
  TEST_ASSERT_EQUAL_INT(200, g.status);
  JsonDocument d;
  deserializeJson(d, g.body);
  TEST_ASSERT_EQUAL_STRING("manual", d["mode"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(409, handleApiEx(ctx, "POST", "/api/updates/install", "").status);       // nothing found yet
  TEST_ASSERT_EQUAL_STRING("no_update", [&] { deserializeJson(d, handleApiEx(ctx, "POST", "/api/updates/install", "").body); return d["error"].as<const char*>(); }());

  HttpResponse c = handleApiEx(ctx, "POST", "/api/updates/check", "");
  TEST_ASSERT_EQUAL_INT(202, c.status);
  TEST_ASSERT_EQUAL_INT(409, handleApiEx(ctx, "POST", "/api/updates/check", "").status);        // already queued
  e.manager.runPending(e.source, e.installer);
  deserializeJson(d, handleApiEx(ctx, "GET", "/api/updates", "").body);
  TEST_ASSERT_TRUE(d["available"].as<bool>());
  JsonDocument st;
  deserializeJson(st, handleApiEx(ctx, "GET", "/api/status", "").body);
  TEST_ASSERT_TRUE(st["update"]["available"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("0.7.0", st["update"]["version"].as<const char*>());
  TEST_ASSERT_TRUE(parse_is_null(handleApiEx(none, "GET", "/api/status", "").body));

  HttpResponse put = handleApiEx(ctx, "PUT", "/api/updates", g.body);   // a GET body can be sent back
  TEST_ASSERT_EQUAL_INT(200, put.status);
  put = handleApiEx(ctx, "PUT", "/api/updates", "{\"mode\":\"automatic\",\"repoUrl\":\"https://github.com/someone/fork\"}");
  TEST_ASSERT_EQUAL_INT(200, put.status);
  deserializeJson(d, put.body);
  TEST_ASSERT_EQUAL_STRING("automatic", d["mode"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("https://github.com/someone/fork", d["repoUrl"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(ctx, "PUT", "/api/updates", "{\"mode\":\"sometimes\"}").status);
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(ctx, "PUT", "/api/updates", "{\"mode\":5}").status);
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(ctx, "PUT", "/api/updates", "{\"checkTime\":\"25:00\"}").status);
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(ctx, "PUT", "/api/updates", "{\"checkTime\":3}").status);
  deserializeJson(d, handleApiEx(ctx, "PUT", "/api/updates", "{\"checkTime\":\"04:15\"}").body);
  TEST_ASSERT_EQUAL_STRING("04:15", d["checkTime"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(ctx, "PUT", "/api/updates", "{\"repoUrl\":\"http://github.com/a/b\"}").status);
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(ctx, "PUT", "/api/updates", "{\"repoUrl\":7}").status);
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(ctx, "PUT", "/api/updates", "{\"nope\":1}").status);
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(ctx, "PUT", "/api/updates", "[]").status);
  TEST_ASSERT_EQUAL_INT(405, handleApiEx(ctx, "POST", "/api/updates", "").status);
  TEST_ASSERT_EQUAL_INT(405, handleApiEx(ctx, "GET", "/api/updates/check", "").status);
  TEST_ASSERT_EQUAL_INT(413, handleApiEx(ctx, "PUT", "/api/updates", std::string(kMaxSmallBodyBytes + 1, ' ')).status);

  e.clock.unixTime = 0;
  TEST_ASSERT_EQUAL_INT(409, handleApiEx(ctx, "POST", "/api/updates/check", "").status);        // no clock
  e.blobs.failPuts = true;
  TEST_ASSERT_EQUAL_INT(507, handleApiEx(ctx, "PUT", "/api/updates", "{\"mode\":\"ignore\"}").status);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_versions_parse_and_compare);
  RUN_TEST(test_repository_addresses);
  RUN_TEST(test_a_release_is_read_for_its_version_and_firmware);
  RUN_TEST(test_release_problems_have_their_own_codes);
  RUN_TEST(test_the_official_firmware_file_wins_over_other_bin_files);
  RUN_TEST(test_settings_default_persist_and_validate);
  RUN_TEST(test_a_full_storage_changes_nothing);
  RUN_TEST(test_a_check_finds_a_newer_release);
  RUN_TEST(test_the_same_or_an_older_release_is_not_an_update);
  RUN_TEST(test_failures_are_reported_in_words);
  RUN_TEST(test_requests_are_refused_while_busy_or_without_a_clock);
  RUN_TEST(test_an_install_writes_reports_progress_and_asks_for_a_restart);
  RUN_TEST(test_after_the_restart_the_attempt_is_forgotten_when_the_new_version_runs);
  RUN_TEST(test_an_image_that_did_not_take_is_marked_blocked);
  RUN_TEST(test_a_failed_install_reports_and_does_not_block);
  RUN_TEST(test_manual_mode_never_installs_by_itself);
  RUN_TEST(test_automatic_mode_installs_what_it_finds);
  RUN_TEST(test_automatic_mode_does_not_retry_a_failed_or_blocked_version);
  RUN_TEST(test_the_daily_check_waits_for_its_time_the_clock_and_the_wifi);
  RUN_TEST(test_it_looks_once_a_day_not_again_until_the_next_day);
  RUN_TEST(test_a_board_that_was_off_at_the_chosen_time_looks_when_it_is_up_again);
  RUN_TEST(test_the_time_is_a_setting_that_persists_and_changes_when_it_looks);
  RUN_TEST(test_times_of_day_are_parsed_and_written);
  RUN_TEST(test_a_failed_check_is_retried_within_the_hour);
  RUN_TEST(test_ignore_mode_never_checks_by_itself_but_check_now_still_works);
  RUN_TEST(test_automatic_mode_installs_at_the_chosen_time);
  RUN_TEST(test_changing_the_repository_forgets_what_was_found_there);
  RUN_TEST(test_update_routes);
  return UNITY_END();
}
