#include <unity.h>

#include <ArduinoJson.h>

#include "api_ex.h"
#include "controller.h"
#include "fakes.h"
#include "schedule.h"

using namespace jig;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint8_t kMon = 1 << 0, kTue = 1 << 1, kWed = 1 << 2, kThu = 1 << 3, kFri = 1 << 4, kSat = 1 << 5, kSun = 1 << 6;
constexpr uint8_t kWeekdays = kMon | kTue | kWed | kThu | kFri;

Schedule entry(const char* id, uint8_t days, int start, int dur, bool enabled = true) {
  Schedule s;
  s.id = id;
  s.days = days;
  s.startMinute = static_cast<uint16_t>(start);
  s.durationMin = static_cast<uint16_t>(dur);
  s.enabled = enabled;
  return s;
}

Schedule limited(Schedule s, int times, int remaining = -1) {
  s.times = static_cast<uint16_t>(times);
  s.remaining = static_cast<uint16_t>(remaining < 0 ? times : remaining);
  return s;
}

LocalTime at(int weekday, int hour, int minute) { return LocalTime{static_cast<uint8_t>(weekday), static_cast<uint16_t>(hour * 60 + minute)}; }

struct Env {
  FakeClock clock;
  XorShiftRng rng{1};
  MemStore config;
  Controller controller{clock, rng, config};
  FlakyBlobStore blobs;
  ScheduleEngine engine{clock, controller, blobs};
  Env() {
    controller.begin();
    engine.begin();
  }
  void setSchedules(std::vector<Schedule> list) {
    ScheduleSettings s;
    s.schedules = std::move(list);
    CmdResult r = engine.replace(s);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message.c_str());
  }
  bool running() { return controller.status().runState == RunState::Running; }
  void tickAt(int weekday, int hour, int minute) {
    clock.local = at(weekday, hour, minute);
    engine.tick();
  }
};

JsonDocument parse(const std::string& s) {
  JsonDocument doc;
  TEST_ASSERT_TRUE_MESSAGE(!deserializeJson(doc, s), s.c_str());
  return doc;
}

}  // namespace

void test_window_covers_the_start_minute_but_not_the_end_minute() {
  const Schedule s = entry("w", kMon, 9 * 60, 60);
  TEST_ASSERT_FALSE(scheduleWindowContains(s, at(0, 8, 59)));
  TEST_ASSERT_TRUE(scheduleWindowContains(s, at(0, 9, 0)));
  TEST_ASSERT_TRUE(scheduleWindowContains(s, at(0, 9, 59)));
  TEST_ASSERT_FALSE(scheduleWindowContains(s, at(0, 10, 0)));
  TEST_ASSERT_FALSE(scheduleWindowContains(s, at(1, 9, 30)));  // Tuesday is not selected
}

void test_window_that_crosses_midnight_belongs_to_the_day_it_started() {
  const Schedule s = entry("night", kMon, 22 * 60, 8 * 60);  // Monday 22:00 for 8 h
  TEST_ASSERT_FALSE(scheduleWindowContains(s, at(0, 21, 59)));
  TEST_ASSERT_TRUE(scheduleWindowContains(s, at(0, 23, 30)));
  TEST_ASSERT_TRUE(scheduleWindowContains(s, at(1, 5, 59)));   // Tuesday early morning
  TEST_ASSERT_FALSE(scheduleWindowContains(s, at(1, 6, 0)));
  TEST_ASSERT_FALSE(scheduleWindowContains(s, at(0, 5, 0)));   // Monday morning: the window starts that evening
}

void test_window_wraps_from_sunday_to_monday() {
  const Schedule s = entry("late", kSun, 23 * 60, 120);
  TEST_ASSERT_TRUE(scheduleWindowContains(s, at(6, 23, 30)));
  TEST_ASSERT_TRUE(scheduleWindowContains(s, at(0, 0, 30)));
  TEST_ASSERT_FALSE(scheduleWindowContains(s, at(0, 1, 0)));
}

void test_a_full_day_window_covers_everything_on_its_days() {
  const Schedule s = entry("all", kWeekdays | kSat | kSun, 0, 1440);
  for (int d = 0; d < 7; ++d) {
    TEST_ASSERT_TRUE(scheduleWindowContains(s, at(d, 0, 0)));
    TEST_ASSERT_TRUE(scheduleWindowContains(s, at(d, 23, 59)));
  }
}

void test_validation() {
  ScheduleSettings s;
  s.schedules = {entry("ok", kMon, 540, 60)};
  TEST_ASSERT_FALSE(validateSchedules(s).has_value());

  auto bad = [&](Schedule e, const char* field) {
    ScheduleSettings x;
    x.schedules = {e};
    auto err = validateSchedules(x);
    TEST_ASSERT_TRUE_MESSAGE(err.has_value(), field);
    TEST_ASSERT_EQUAL_STRING(field, err->field.c_str());
  };
  bad(entry("ok", 0, 540, 60), "schedules[0].days");
  bad(entry("ok", 0x80, 540, 60), "schedules[0].days");
  bad(entry("ok", kMon, 1440, 60), "schedules[0].start");
  bad(entry("ok", kMon, 540, 0), "schedules[0].durationMin");
  bad(entry("ok", kMon, 540, 1441), "schedules[0].durationMin");
  bad(entry("", kMon, 540, 60), "schedules[0].id");
  bad(entry("Has Space", kMon, 540, 60), "schedules[0].id");

  ScheduleSettings dup;
  dup.schedules = {entry("a", kMon, 1, 1), entry("a", kTue, 1, 1)};
  TEST_ASSERT_EQUAL_STRING("schedules[1].id", validateSchedules(dup)->field.c_str());

  ScheduleSettings many;
  for (size_t i = 0; i <= kMaxSchedules; ++i) many.schedules.push_back(entry(("s" + std::to_string(i)).c_str(), kMon, 1, 1));
  TEST_ASSERT_EQUAL_STRING("schedules", validateSchedules(many)->field.c_str());

  ScheduleSettings tz;
  tz.timezone = "Mars/Olympus";
  TEST_ASSERT_EQUAL_STRING("timezone", validateSchedules(tz)->field.c_str());
}

void test_json_round_trip() {
  const std::string body =
      "{\"timezone\":\"Europe/London\",\"schedules\":[{\"id\":\"work\",\"enabled\":true,"
      "\"days\":[\"mon\",\"tue\",\"wed\",\"thu\",\"fri\"],\"start\":\"09:30\",\"durationMin\":480}]}";
  ScheduleSettings s;
  ParseError err;
  TEST_ASSERT_TRUE_MESSAGE(parseSchedules(body, s, err), err.message.c_str());
  TEST_ASSERT_EQUAL_STRING("Europe/London", s.timezone.c_str());
  TEST_ASSERT_EQUAL_UINT32(1, s.schedules.size());
  TEST_ASSERT_EQUAL_UINT8(kWeekdays, s.schedules[0].days);
  TEST_ASSERT_EQUAL_UINT16(9 * 60 + 30, s.schedules[0].startMinute);
  TEST_ASSERT_EQUAL_UINT16(480, s.schedules[0].durationMin);
  JsonDocument d = parse(schedulesToJson(s));
  TEST_ASSERT_EQUAL_STRING("09:30", d["schedules"][0]["start"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(5, d["schedules"][0]["days"].size());
  TEST_ASSERT_EQUAL_STRING("mon", d["schedules"][0]["days"][0].as<const char*>());
  TEST_ASSERT_TRUE(d["timezones"].size() > 5);  // the list the dashboard offers
}

void test_parse_rejects_malformed_input_with_a_field() {
  ScheduleSettings s;
  ParseError err;
  struct Case { const char* body; const char* field; };
  const Case cases[] = {
      {"nope", ""},
      {"{\"schedules\":5}", "schedules"},
      {"{\"schedules\":[{\"id\":\"a\",\"days\":[\"funday\"],\"start\":\"09:00\",\"durationMin\":60}]}", "schedules[0].days"},
      {"{\"schedules\":[{\"id\":\"a\",\"days\":[\"mon\"],\"start\":\"9am\",\"durationMin\":60}]}", "schedules[0].start"},
      {"{\"schedules\":[{\"id\":\"a\",\"days\":[\"mon\"],\"start\":\"24:00\",\"durationMin\":60}]}", "schedules[0].start"},
      {"{\"schedules\":[{\"id\":\"a\",\"days\":[\"mon\"],\"start\":\"09:60\",\"durationMin\":60}]}", "schedules[0].start"},
      {"{\"schedules\":[{\"id\":\"a\",\"days\":[\"mon\"],\"start\":\"09:00\",\"durationMin\":\"60\"}]}", "schedules[0].durationMin"},
      {"{\"schedules\":[{\"id\":\"a\",\"days\":[\"mon\"],\"start\":\"09:00\",\"durationMin\":60,\"bogus\":1}]}", "schedules[0].bogus"},
      {"{\"colour\":1}", "colour"},
  };
  for (const Case& c : cases) {
    TEST_ASSERT_FALSE_MESSAGE(parseSchedules(c.body, s, err), c.body);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.field, err.field.c_str(), c.body);
  }
}

void test_settings_persist_across_a_restart() {
  Env e;
  e.setSchedules({entry("work", kWeekdays, 540, 480)});
  ScheduleEngine again(e.clock, e.controller, e.blobs);
  again.begin();
  TEST_ASSERT_EQUAL_UINT32(1, again.settings().schedules.size());
  TEST_ASSERT_EQUAL_STRING("work", again.settings().schedules[0].id.c_str());
}

void test_a_full_storage_is_reported_and_nothing_changes() {
  Env e;
  e.blobs.failPuts = true;
  ScheduleSettings s;
  s.schedules = {entry("work", kMon, 540, 60)};
  CmdResult r = e.engine.replace(s);
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("storage_error", r.error.c_str());
  TEST_ASSERT_TRUE(e.engine.settings().schedules.empty());
}

void test_invalid_settings_are_not_applied() {
  Env e;
  ScheduleSettings s;
  s.schedules = {entry("work", 0, 540, 60)};
  CmdResult r = e.engine.replace(s);
  TEST_ASSERT_EQUAL_STRING("invalid_schedule", r.error.c_str());
  TEST_ASSERT_EQUAL_STRING("schedules[0].days", r.field.c_str());
}

void test_the_window_starts_and_stops_the_jiggler() {
  Env e;
  e.setSchedules({entry("work", kWeekdays, 9 * 60, 60)});
  e.controller.stop();  // these tests are about the window edges, so start from stopped
  e.tickAt(0, 8, 0);   // first look at the clock: outside the window, so it is stopped
  TEST_ASSERT_FALSE(e.running());
  e.tickAt(0, 8, 59);
  TEST_ASSERT_FALSE(e.running());
  e.tickAt(0, 9, 0);
  TEST_ASSERT_TRUE(e.running());
  e.tickAt(0, 9, 59);
  TEST_ASSERT_TRUE(e.running());
  e.tickAt(0, 10, 0);
  TEST_ASSERT_FALSE(e.running());
  e.tickAt(5, 9, 30);  // Saturday: not a selected day
  TEST_ASSERT_FALSE(e.running());
}

void test_boot_inside_a_window_runs_and_outside_it_stays_stopped() {
  Env inside;
  inside.setSchedules({entry("work", kWeekdays, 9 * 60, 480)});
  inside.controller.stop();
  ScheduleEngine fresh(inside.clock, inside.controller, inside.blobs);  // a "reboot": new engine, same storage
  fresh.begin();
  inside.clock.local = at(2, 12, 0);
  fresh.tick();
  TEST_ASSERT_TRUE(inside.running());

  Env outside;  // the controller boots Running by default
  outside.setSchedules({entry("work", kWeekdays, 9 * 60, 60)});
  ScheduleEngine fresh2(outside.clock, outside.controller, outside.blobs);
  fresh2.begin();
  TEST_ASSERT_TRUE(outside.running());
  outside.clock.local = at(2, 20, 0);
  fresh2.tick();
  TEST_ASSERT_FALSE(outside.running());  // schedules are in charge once there is one
}

void test_a_manual_change_inside_the_window_is_respected_until_the_next_edge() {
  Env e;
  e.setSchedules({entry("work", kWeekdays, 9 * 60, 120)});
  e.tickAt(0, 9, 0);
  TEST_ASSERT_TRUE(e.running());
  e.controller.stop();            // the owner switches it off by hand
  e.tickAt(0, 9, 30);
  e.tickAt(0, 10, 0);
  TEST_ASSERT_FALSE(e.running());  // not restarted behind their back
  e.tickAt(0, 11, 0);              // window over: nothing to stop
  TEST_ASSERT_FALSE(e.running());
  e.tickAt(1, 9, 0);               // next day's window starts it again
  TEST_ASSERT_TRUE(e.running());
}

void test_manual_start_outside_a_window_is_left_alone() {
  Env e;
  e.setSchedules({entry("work", kWeekdays, 9 * 60, 60)});
  e.controller.stop();  // these tests are about the window edges, so start from stopped
  e.tickAt(0, 20, 0);
  TEST_ASSERT_FALSE(e.running());
  e.controller.start();
  e.tickAt(0, 20, 30);
  e.tickAt(0, 21, 0);
  TEST_ASSERT_TRUE(e.running());
}

void test_saving_does_not_start_or_stop_by_itself() {
  Env e;
  e.clock.local = at(0, 20, 0);
  e.engine.tick();                  // no schedules yet: the controller is untouched
  TEST_ASSERT_TRUE(e.running());
  e.setSchedules({entry("work", kWeekdays, 9 * 60, 60)});
  e.engine.tick();                  // 20:00 is outside the new window, but saving must not stop a running jiggler
  TEST_ASSERT_TRUE(e.running());
  e.tickAt(1, 9, 0);                // the next window opens: start (already running), then it ends
  e.tickAt(1, 10, 0);
  TEST_ASSERT_FALSE(e.running());
}

void test_disabled_or_empty_schedules_never_touch_the_controller() {
  Env e;
  e.tickAt(0, 3, 0);
  TEST_ASSERT_TRUE(e.running());
  e.setSchedules({entry("off", kWeekdays, 9 * 60, 60, false)});
  e.tickAt(0, 3, 0);
  e.tickAt(0, 9, 30);
  e.tickAt(0, 11, 0);
  TEST_ASSERT_TRUE(e.running());
}

void test_nothing_happens_while_the_clock_is_not_synced_and_then_the_schedule_takes_over_after_a_boot() {
  Env e;
  e.setSchedules({entry("work", kWeekdays, 9 * 60, 60)});
  ScheduleEngine rebooted(e.clock, e.controller, e.blobs);  // the clock is unknown at boot
  rebooted.begin();
  e.clock.local.reset();
  rebooted.tick();
  TEST_ASSERT_TRUE(e.running());
  TEST_ASSERT_FALSE(rebooted.status().timeSynced);
  e.clock.local = at(0, 20, 0);  // SNTP arrives: the schedule is in charge
  rebooted.tick();
  TEST_ASSERT_FALSE(e.running());
  TEST_ASSERT_TRUE(rebooted.status().timeSynced);
}

void test_saving_without_a_clock_still_does_not_stop_anything_when_the_clock_arrives() {
  Env e;
  e.setSchedules({entry("work", kWeekdays, 9 * 60, 60)});  // clock unknown while saving
  e.tickAt(0, 20, 0);                                       // the clock arrives outside the window
  TEST_ASSERT_TRUE(e.running());
}

void test_overlapping_windows_keep_it_running_until_the_last_one_closes() {
  Env e;
  e.setSchedules({entry("a", kWeekdays, 9 * 60, 60), entry("b", kWeekdays, 9 * 60 + 30, 90)});  // 09:00-10:00 and 09:30-11:00
  e.controller.stop();  // these tests are about the window edges, so start from stopped
  e.tickAt(0, 8, 0);
  TEST_ASSERT_FALSE(e.running());
  e.tickAt(0, 9, 0);
  TEST_ASSERT_TRUE(e.running());
  e.tickAt(0, 9, 45);
  TEST_ASSERT_TRUE(e.running());
  e.tickAt(0, 10, 0);   // "a" closed, "b" is still open
  TEST_ASSERT_TRUE(e.running());
  e.tickAt(0, 10, 59);
  TEST_ASSERT_TRUE(e.running());
  e.tickAt(0, 11, 0);
  TEST_ASSERT_FALSE(e.running());
}

void test_enabling_a_schedule_after_all_were_off_starts_from_a_clean_look_at_the_clock() {
  Env e;
  e.setSchedules({entry("work", kWeekdays, 9 * 60, 120, false)});
  e.tickAt(0, 9, 30);            // disabled: untouched
  TEST_ASSERT_TRUE(e.running());
  e.setSchedules({entry("work", kWeekdays, 9 * 60, 120, true)});  // enabled while inside its window
  e.tickAt(0, 9, 40);
  TEST_ASSERT_TRUE(e.running());  // saving did not stop it
  e.tickAt(0, 11, 0);             // and the closing edge still works
  TEST_ASSERT_FALSE(e.running());
}

void test_the_posix_timezone_reaches_the_platform() {
  Env e;
  ScheduleSettings s;
  s.timezone = "America/New_York";
  s.schedules = {entry("a", kMon, 0, 60)};
  TEST_ASSERT_TRUE(e.engine.replace(s).ok);
  e.tickAt(0, 1, 0);
  TEST_ASSERT_EQUAL_STRING("EST5EDT,M3.2.0,M11.1.0", e.clock.lastTz.c_str());
  TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", posixTimezone("Europe/Rome")->c_str());
  TEST_ASSERT_FALSE(posixTimezone("Nowhere/Land").has_value());
}

void test_status_reports_local_time_and_whether_a_window_is_open() {
  Env e;
  e.setSchedules({entry("work", kWeekdays, 9 * 60, 60)});
  e.tickAt(2, 9, 5);
  ScheduleStatus s = e.engine.status();
  TEST_ASSERT_TRUE(s.timeSynced);
  TEST_ASSERT_EQUAL_STRING("09:05", s.localTime.c_str());
  TEST_ASSERT_EQUAL_STRING("wed", s.weekday.c_str());
  TEST_ASSERT_TRUE(s.active);
  TEST_ASSERT_EQUAL_UINT32(1, s.enabledCount);
  e.tickAt(2, 12, 0);
  TEST_ASSERT_FALSE(e.engine.status().active);
}

void test_schedule_routes_and_status_extras() {
  Env e;
  ApiContext ctx{e.controller};
  ctx.schedules = &e.engine;
  HttpResponse g = handleApiEx(ctx, "GET", "/api/schedules", "");
  TEST_ASSERT_EQUAL_INT(200, g.status);
  TEST_ASSERT_TRUE(parse(g.body)["schedules"].size() == 0);

  const std::string body =
      "{\"timezone\":\"Europe/Rome\",\"schedules\":[{\"id\":\"work\",\"enabled\":true,"
      "\"days\":[\"mon\",\"fri\"],\"start\":\"08:00\",\"durationMin\":600}]}";
  HttpResponse put = handleApiEx(ctx, "PUT", "/api/schedules", body);
  TEST_ASSERT_EQUAL_INT(200, put.status);
  TEST_ASSERT_EQUAL_STRING("08:00", parse(put.body)["schedules"][0]["start"].as<const char*>());

  HttpResponse bad = handleApiEx(ctx, "PUT", "/api/schedules", "{\"schedules\":[{\"id\":\"x\",\"days\":[],\"start\":\"08:00\",\"durationMin\":5}]}");
  TEST_ASSERT_EQUAL_INT(400, bad.status);
  TEST_ASSERT_EQUAL_STRING("schedules[0].days", parse(bad.body)["field"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(405, handleApiEx(ctx, "POST", "/api/schedules", "").status);
  TEST_ASSERT_EQUAL_INT(413, handleApiEx(ctx, "PUT", "/api/schedules", std::string(kMaxMediumBodyBytes + 1, ' ')).status);

  e.tickAt(0, 9, 0);
  JsonDocument st = parse(handleApiEx(ctx, "GET", "/api/status", "").body);
  TEST_ASSERT_TRUE(st["schedule"]["timeSynced"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("09:00", st["schedule"]["localTime"].as<const char*>());
  TEST_ASSERT_TRUE(st["schedule"]["active"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("Europe/Rome", st["schedule"]["timezone"].as<const char*>());

  e.blobs.failPuts = true;
  TEST_ASSERT_EQUAL_INT(507, handleApiEx(ctx, "PUT", "/api/schedules", body).status);

  ApiContext none{e.controller};  // no service: 404, and no "schedule" key in status
  TEST_ASSERT_EQUAL_INT(404, handleApiEx(none, "GET", "/api/schedules", "").status);
  TEST_ASSERT_TRUE(parse(handleApiEx(none, "GET", "/api/status", "").body)["schedule"].isNull());
}

void test_a_window_that_opens_before_any_computer_is_paired_starts_once_one_is() {
  Env e;
  e.controller.stop();
  e.setSchedules({entry("work", kWeekdays, 9 * 60, 120)});
  e.clock.paired = false;
  e.tickAt(0, 9, 0);                  // the window opens, nothing is paired: it cannot start
  TEST_ASSERT_FALSE(e.running());
  e.clock.paired = true;              // a computer pairs while the window is still open
  e.tickAt(0, 9, 30);
  TEST_ASSERT_TRUE(e.running());
  e.tickAt(0, 11, 0);
  TEST_ASSERT_FALSE(e.running());
}

void test_repeat_count_is_validated() {
  ScheduleSettings s;
  s.schedules = {limited(entry("a", kMon, 540, 60), 3)};
  TEST_ASSERT_FALSE(validateSchedules(s).has_value());
  s.schedules = {limited(entry("a", kMon, 540, 60), 1000)};
  TEST_ASSERT_EQUAL_STRING("schedules[0].times", validateSchedules(s)->field.c_str());
  s.schedules = {limited(entry("a", kMon, 540, 60), 3, 4)};
  TEST_ASSERT_EQUAL_STRING("schedules[0].remaining", validateSchedules(s)->field.c_str());
}

void test_repeat_count_in_json() {
  const std::string body =
      "{\"schedules\":[{\"id\":\"a\",\"days\":[\"mon\"],\"start\":\"09:00\",\"durationMin\":60,\"times\":3,\"remaining\":2},"
      "{\"id\":\"b\",\"days\":[\"tue\"],\"start\":\"09:00\",\"durationMin\":60,\"times\":null}]}";
  ScheduleSettings s;
  ParseError err;
  TEST_ASSERT_TRUE_MESSAGE(parseSchedules(body, s, err), err.message.c_str());
  TEST_ASSERT_EQUAL_UINT16(3, s.schedules[0].times);
  TEST_ASSERT_EQUAL_UINT16(2, s.schedules[0].remaining);
  TEST_ASSERT_EQUAL_UINT16(0, s.schedules[1].times);                 // null = forever
  JsonDocument d = parse(schedulesToJson(s));
  TEST_ASSERT_EQUAL_UINT32(3, d["schedules"][0]["times"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(2, d["schedules"][0]["remaining"].as<uint32_t>());
  TEST_ASSERT_TRUE(d["schedules"][1]["times"].isNull());
  TEST_ASSERT_TRUE(d["schedules"][1]["remaining"].isNull());
  ScheduleSettings bad;
  TEST_ASSERT_FALSE(parseSchedules("{\"schedules\":[{\"id\":\"a\",\"days\":[\"mon\"],\"start\":\"09:00\",\"durationMin\":60,\"times\":\"x\"}]}", bad, err));
  TEST_ASSERT_EQUAL_STRING("schedules[0].times", err.field.c_str());
  TEST_ASSERT_FALSE(parseSchedules("{\"schedules\":[{\"id\":\"a\",\"days\":[\"mon\"],\"start\":\"09:00\",\"durationMin\":60,\"times\":0}]}", bad, err));
}

void test_a_schedule_with_a_count_runs_that_many_times_then_turns_itself_off() {
  Env e;
  e.setSchedules({limited(entry("work", kWeekdays, 9 * 60, 60), 2)});
  e.controller.stop();
  e.tickAt(0, 8, 0);
  e.tickAt(0, 9, 0);                                   // 1st window opens
  TEST_ASSERT_TRUE(e.running());
  TEST_ASSERT_EQUAL_UINT16(1, e.engine.settings().schedules[0].remaining);
  e.tickAt(0, 10, 0);
  TEST_ASSERT_FALSE(e.running());
  e.tickAt(1, 9, 0);                                   // 2nd window opens: the last one
  TEST_ASSERT_TRUE(e.running());
  TEST_ASSERT_EQUAL_UINT16(0, e.engine.settings().schedules[0].remaining);
  TEST_ASSERT_TRUE(e.engine.settings().schedules[0].enabled);   // still on until that window ends
  e.tickAt(1, 9, 30);
  TEST_ASSERT_TRUE(e.running());
  e.tickAt(1, 10, 0);
  TEST_ASSERT_FALSE(e.running());
  TEST_ASSERT_FALSE(e.engine.settings().schedules[0].enabled);  // finished
  e.tickAt(2, 9, 0);                                   // a third window never starts
  TEST_ASSERT_FALSE(e.running());
}

void test_a_forever_schedule_never_runs_out() {
  Env e;
  e.setSchedules({entry("work", kWeekdays, 9 * 60, 60)});
  e.controller.stop();
  for (int d = 0; d < 5; ++d) { e.tickAt(d, 9, 0); TEST_ASSERT_TRUE(e.running()); e.tickAt(d, 10, 0); }
  TEST_ASSERT_TRUE(e.engine.settings().schedules[0].enabled);
  TEST_ASSERT_EQUAL_UINT16(0, e.engine.settings().schedules[0].times);
}

void test_the_count_survives_a_restart_and_a_window_in_progress_is_not_counted_twice() {
  Env e;
  e.setSchedules({limited(entry("work", kWeekdays, 9 * 60, 120), 3)});
  e.controller.stop();
  e.tickAt(0, 9, 0);
  TEST_ASSERT_EQUAL_UINT16(2, e.engine.settings().schedules[0].remaining);
  ScheduleEngine rebooted(e.clock, e.controller, e.blobs);        // power cut in the middle of the window
  rebooted.begin();
  TEST_ASSERT_EQUAL_UINT16(2, rebooted.settings().schedules[0].remaining);
  e.clock.local = at(0, 9, 30);
  rebooted.tick();                                                // resumes the window it was in
  TEST_ASSERT_EQUAL_UINT16(2, rebooted.settings().schedules[0].remaining);
  TEST_ASSERT_TRUE(e.running());
}

void test_overlapping_counted_schedules_count_independently() {
  Env e;
  e.setSchedules({limited(entry("a", kWeekdays, 9 * 60, 60), 1), entry("b", kWeekdays, 9 * 60 + 30, 90)});
  e.controller.stop();
  e.tickAt(0, 9, 0);
  e.tickAt(0, 10, 0);                                  // "a" closes and is spent, "b" is still open
  TEST_ASSERT_TRUE(e.running());
  TEST_ASSERT_FALSE(e.engine.settings().schedules[0].enabled);
  TEST_ASSERT_TRUE(e.engine.settings().schedules[1].enabled);
  e.tickAt(0, 11, 0);
  TEST_ASSERT_FALSE(e.running());
}

void test_saving_keeps_the_progress_and_turning_a_finished_schedule_back_on_rearms_it() {
  Env e;
  e.setSchedules({limited(entry("work", kWeekdays, 9 * 60, 60), 3, 1)});
  TEST_ASSERT_EQUAL_UINT16(1, e.engine.settings().schedules[0].remaining);   // sent as it was
  Schedule finished = limited(entry("work", kWeekdays, 9 * 60, 60), 3, 0);
  finished.enabled = false;
  e.setSchedules({finished});
  TEST_ASSERT_FALSE(e.engine.settings().schedules[0].enabled);
  finished.enabled = true;                                                    // the owner switches it back on
  e.setSchedules({finished});
  TEST_ASSERT_EQUAL_UINT16(3, e.engine.settings().schedules[0].remaining);   // a fresh run of 3
  Schedule forever = entry("f", kMon, 0, 60);
  forever.remaining = 5;                                                      // meaningless without a count
  e.setSchedules({forever});
  TEST_ASSERT_EQUAL_UINT16(0, e.engine.settings().schedules[0].remaining);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_repeat_count_is_validated);
  RUN_TEST(test_repeat_count_in_json);
  RUN_TEST(test_a_schedule_with_a_count_runs_that_many_times_then_turns_itself_off);
  RUN_TEST(test_a_forever_schedule_never_runs_out);
  RUN_TEST(test_the_count_survives_a_restart_and_a_window_in_progress_is_not_counted_twice);
  RUN_TEST(test_overlapping_counted_schedules_count_independently);
  RUN_TEST(test_saving_keeps_the_progress_and_turning_a_finished_schedule_back_on_rearms_it);
  RUN_TEST(test_window_covers_the_start_minute_but_not_the_end_minute);
  RUN_TEST(test_window_that_crosses_midnight_belongs_to_the_day_it_started);
  RUN_TEST(test_window_wraps_from_sunday_to_monday);
  RUN_TEST(test_a_full_day_window_covers_everything_on_its_days);
  RUN_TEST(test_validation);
  RUN_TEST(test_json_round_trip);
  RUN_TEST(test_parse_rejects_malformed_input_with_a_field);
  RUN_TEST(test_settings_persist_across_a_restart);
  RUN_TEST(test_a_full_storage_is_reported_and_nothing_changes);
  RUN_TEST(test_invalid_settings_are_not_applied);
  RUN_TEST(test_the_window_starts_and_stops_the_jiggler);
  RUN_TEST(test_boot_inside_a_window_runs_and_outside_it_stays_stopped);
  RUN_TEST(test_a_manual_change_inside_the_window_is_respected_until_the_next_edge);
  RUN_TEST(test_manual_start_outside_a_window_is_left_alone);
  RUN_TEST(test_saving_does_not_start_or_stop_by_itself);
  RUN_TEST(test_disabled_or_empty_schedules_never_touch_the_controller);
  RUN_TEST(test_nothing_happens_while_the_clock_is_not_synced_and_then_the_schedule_takes_over_after_a_boot);
  RUN_TEST(test_saving_without_a_clock_still_does_not_stop_anything_when_the_clock_arrives);
  RUN_TEST(test_overlapping_windows_keep_it_running_until_the_last_one_closes);
  RUN_TEST(test_a_window_that_opens_before_any_computer_is_paired_starts_once_one_is);
  RUN_TEST(test_enabling_a_schedule_after_all_were_off_starts_from_a_clean_look_at_the_clock);
  RUN_TEST(test_the_posix_timezone_reaches_the_platform);
  RUN_TEST(test_status_reports_local_time_and_whether_a_window_is_open);
  RUN_TEST(test_schedule_routes_and_status_extras);
  return UNITY_END();
}
