#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "api_ex.h"
#include "automation.h"
#include "controller.h"
#include "fakes.h"
#include "mcp_server.h"

using namespace jig;

void setUp() {}
void tearDown() {}

namespace {

struct MemNetworkStore : NetworkStore {
  NetworkSettings stored;
  int saves = 0;
  NetworkSettings load() override { return stored; }
  void save(const NetworkSettings& s) override {
    stored = s;
    saves++;
  }
};

struct Env {
  FakeClock clock;
  XorShiftRng rng{1};
  MemStore config;
  Controller controller{clock, rng, config};
  MemBlobStore blobs;
  MemNetworkStore net;
  DeviceRegistry devices{blobs};
  ScriptLibrary scripts{blobs};
  AutomationEngine automations{clock, rng, scripts, blobs};
  ScheduleEngine schedules{clock, controller, blobs};
  struct Link : BleLink {
    std::vector<std::string> bonded{"aa:bb:cc:dd:ee:01"}, connected;
    int disconnects = 0;
    bool advertising = true, pairingOpen = false;
    std::vector<std::string> bondedAddresses() override { return bonded; }
    std::vector<std::string> connectedAddresses() override { return connected; }
    void disconnectAll() override { disconnects++; connected.clear(); }
    bool forgetBond(const std::string& a) override { bonded.clear(); return !a.empty(); }
    void forgetAllBonds() override { bonded.clear(); }
    void setAdvertising(bool on) override { advertising = on; }
    void setPairingOpen(bool open) override { pairingOpen = open; }
    bool takePeerDisconnectRequest() override { return false; }
  } link;
  BluetoothManager bluetooth{clock, link, blobs};
  int restarts = 0;
  ApiContext ctx{controller};

  Env() {
    controller.begin();
    devices.begin();
    automations.begin();
    schedules.begin();
    bluetooth.begin();
    ctx.bluetooth = &bluetooth;
    ctx.schedules = &schedules;
    ctx.network = &net;
    ctx.devices = &devices;
    ctx.scripts = &scripts;
    ctx.automations = &automations;
    ctx.restart = [this] { restarts++; };
  }

  McpReply call(const std::string& body) { return handleMcpEx(ctx, body); }

  McpReply tool(const std::string& name, const std::string& argsJson = "{}") {
    return call("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{\"name\":\"" + name +
                "\",\"arguments\":" + argsJson + "}}");
  }
};

JsonDocument parse(const std::string& s) {
  JsonDocument doc;
  // tools/list nests deeper than ArduinoJson's default limit of 10 (set_devices schema)
  TEST_ASSERT_TRUE_MESSAGE(!deserializeJson(doc, s, DeserializationOption::NestingLimit(32)), s.c_str());
  return doc;
}

JsonDocument toolText(const McpReply& r) {
  JsonDocument reply = parse(r.body);
  TEST_ASSERT_EQUAL_STRING("text", reply["result"]["content"][0]["type"].as<const char*>());
  return parse(reply["result"]["content"][0]["text"].as<std::string>());
}

bool isError(const McpReply& r) {
  JsonDocument reply = parse(r.body);
  TEST_ASSERT_TRUE_MESSAGE(reply["result"]["isError"].is<bool>(), r.body.c_str());
  return reply["result"]["isError"].as<bool>();
}

const char* kDemoScript = "{\"steps\":[{\"op\":\"click\"}]}";

}  // namespace

void test_tools_list_includes_new_tools() {
  Env e;
  McpReply r = e.call("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}");
  JsonDocument doc = parse(r.body);  // keep the document alive while `tools` points into it
  JsonArray tools = doc["result"]["tools"].as<JsonArray>();
  const char* expected[] = {"get_status", "start", "pause", "stop", "move_now", "set_config",
                            "list_profiles", "get_devices", "set_devices", "list_scripts", "save_script",
                            "delete_script", "run_script", "get_automations", "set_automations", "press_key",
                            "type_text", "get_network", "set_network", "restart", "get_bluetooth", "start_pairing",
                            "disconnect_bluetooth", "reconnect_bluetooth", "forget_computer",
                            "get_schedules", "set_schedules"};
  const size_t n = sizeof(expected) / sizeof(expected[0]);
  TEST_ASSERT_EQUAL_UINT32(n, tools.size());
  for (size_t i = 0; i < n; i++) {
    TEST_ASSERT_EQUAL_STRING(expected[i], tools[i]["name"].as<const char*>());
    TEST_ASSERT_TRUE(tools[i]["description"].is<const char*>());
    TEST_ASSERT_EQUAL_STRING("object", tools[i]["inputSchema"]["type"].as<const char*>());
  }
}

void test_schedule_tools() {
  Env e;
  TEST_ASSERT_FALSE(isError(e.tool("get_schedules")));
  const std::string args =
      "{\"timezone\":\"Europe/Rome\",\"schedules\":[{\"id\":\"work\",\"enabled\":true,\"days\":[\"mon\",\"tue\"],"
      "\"start\":\"09:00\",\"durationMin\":480}]}";
  McpReply set = e.tool("set_schedules", args);
  TEST_ASSERT_FALSE(isError(set));
  TEST_ASSERT_EQUAL_STRING("work", toolText(e.tool("get_schedules"))["schedules"][0]["id"].as<const char*>());
  TEST_ASSERT_TRUE(isError(e.tool("set_schedules", "{\"schedules\":[{\"id\":\"x\",\"days\":[],\"start\":\"09:00\",\"durationMin\":5}]}")));
  e.ctx.schedules = nullptr;
  TEST_ASSERT_TRUE(parse(e.tool("get_schedules").body)["error"].is<JsonObject>());  // unknown tool without the service
}

void test_tools_without_services_are_not_listed() {
  Env e;
  e.ctx.network = nullptr;
  e.ctx.restart = nullptr;
  McpReply r = e.call("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}");
  JsonDocument doc = parse(r.body);
  JsonArray tools = doc["result"]["tools"].as<JsonArray>();
  for (JsonObject t : tools) {
    const std::string name = t["name"].as<std::string>();
    TEST_ASSERT_TRUE_MESSAGE(name != "get_network" && name != "set_network" && name != "restart", name.c_str());
  }
  McpReply call = e.tool("get_network");
  JsonDocument d = parse(call.body);
  TEST_ASSERT_EQUAL_INT(-32602, d["error"]["code"].as<int>());
}

void test_legacy_mcp_matches_ex_without_services() {
  FakeClock clock;
  XorShiftRng rng(1);
  MemStore config;
  Controller controller(clock, rng, config);
  controller.begin();
  ApiContext bare{controller};
  const std::string req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}";
  McpReply legacy = handleMcp(controller, req);
  McpReply ex = handleMcpEx(bare, req);
  TEST_ASSERT_EQUAL_INT(legacy.httpStatus, ex.httpStatus);
  TEST_ASSERT_EQUAL_STRING(legacy.body.c_str(), ex.body.c_str());
  // the legacy entry point does not know the new tools
  McpReply unknown = handleMcp(controller, "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"list_profiles\",\"arguments\":{}}}");
  TEST_ASSERT_EQUAL_INT(-32602, parse(unknown.body)["error"]["code"].as<int>());
}

void test_devices_tools() {
  Env e;
  McpReply p = e.tool("list_profiles");
  TEST_ASSERT_FALSE(isError(p));
  TEST_ASSERT_TRUE(toolText(p)["profiles"].as<JsonArray>().size() >= 5);
  McpReply g = e.tool("get_devices");
  TEST_ASSERT_EQUAL_UINT32(2, toolText(g)["devices"].as<JsonArray>().size());

  McpReply ok = e.tool("set_devices",
      "{\"devices\":[{\"id\":\"mouse\",\"kind\":\"mouse\",\"enabled\":true,\"profileId\":\"razer-orochi-v2\"},"
      "{\"id\":\"keyboard\",\"kind\":\"keyboard\",\"enabled\":true,\"profileId\":\"logitech-mx-keys\"}]}");
  TEST_ASSERT_FALSE(isError(ok));
  TEST_ASSERT_TRUE(toolText(ok)["restartRequired"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("razer-orochi-v2", e.devices.list()[0].profileId.c_str());

  McpReply bad = e.tool("set_devices", "{\"devices\":[{\"id\":\"mouse\",\"kind\":\"mouse\",\"enabled\":true,\"mac\":\"zz\"}]}");
  TEST_ASSERT_TRUE(isError(bad));
  TEST_ASSERT_EQUAL_STRING("devices[0].mac", toolText(bad)["field"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("razer-orochi-v2", e.devices.list()[0].profileId.c_str());

  McpReply missing = e.tool("set_devices", "{}");
  TEST_ASSERT_TRUE(isError(missing));
}

void test_script_tools_roundtrip() {
  Env e;
  McpReply save = e.tool("save_script", std::string("{\"name\":\"demo\",\"script\":") + kDemoScript + "}");
  TEST_ASSERT_FALSE(isError(save));
  McpReply list = e.tool("list_scripts");
  TEST_ASSERT_EQUAL_UINT32(2, toolText(list)["scripts"].as<JsonArray>().size());

  McpReply run = e.tool("run_script", "{\"name\":\"demo\"}");
  TEST_ASSERT_FALSE(isError(run));
  TEST_ASSERT_TRUE(toolText(run)["queued"].as<bool>());
  auto plan = e.automations.tick(false);
  TEST_ASSERT_TRUE(plan.has_value());
  TEST_ASSERT_TRUE(plan->steps[0].kind == Step::Kind::Click);

  McpReply bad = e.tool("save_script", "{\"name\":\"rotto\",\"script\":{\"steps\":[{\"op\":\"nope\"}]}}");
  TEST_ASSERT_TRUE(isError(bad));
  TEST_ASSERT_EQUAL_STRING("/steps/0/op", toolText(bad)["field"].as<const char*>());
  TEST_ASSERT_TRUE(isError(e.tool("save_script", "{\"name\":\"x\"}")));
  TEST_ASSERT_TRUE(isError(e.tool("save_script", "{\"script\":{}}")));

  McpReply del = e.tool("delete_script", "{\"name\":\"demo\"}");
  TEST_ASSERT_FALSE(isError(del));
  TEST_ASSERT_TRUE(isError(e.tool("delete_script", "{\"name\":\"demo\"}")));
  TEST_ASSERT_TRUE(isError(e.tool("delete_script", "{\"name\":\"keepalive-key\"}")));
}

void test_run_script_errors() {
  Env e;
  TEST_ASSERT_TRUE(isError(e.tool("run_script", "{\"name\":\"nope\"}")));
  TEST_ASSERT_TRUE(isError(e.tool("run_script", "{}")));
  e.clock.mouse = false;
  McpReply off = e.tool("run_script", "{\"name\":\"keepalive-key\"}");
  TEST_ASSERT_TRUE(isError(off));
  TEST_ASSERT_EQUAL_STRING("mouse_not_connected", toolText(off)["error"].as<const char*>());
}

void test_automation_tools() {
  Env e;
  McpReply g = e.tool("get_automations");
  TEST_ASSERT_EQUAL_STRING("keepalive-key", toolText(g)["automations"][0]["id"].as<const char*>());
  McpReply ok = e.tool("set_automations",
      "{\"automations\":[{\"id\":\"k\",\"script\":\"keepalive-key\",\"minIntervalSec\":30,\"maxIntervalSec\":40,\"enabled\":true}]}");
  TEST_ASSERT_FALSE(isError(ok));
  TEST_ASSERT_TRUE(e.automations.list()[0].enabled);
  TEST_ASSERT_TRUE(isError(e.tool("set_automations", "{\"automations\":[{\"id\":\"k\",\"script\":\"nope\",\"minIntervalSec\":30,\"maxIntervalSec\":40,\"enabled\":true}]}")));
  TEST_ASSERT_TRUE(isError(e.tool("set_automations", "{}")));
}

void test_type_text_tool() {  // RF3
  Env e;
  McpReply ok = e.tool("type_text", "{\"text\":\"Hi!\"}");
  TEST_ASSERT_FALSE(isError(ok));
  TEST_ASSERT_TRUE(toolText(ok)["queued"].as<bool>());
  auto plan = e.automations.tick(false);
  TEST_ASSERT_TRUE(plan.has_value());
  TEST_ASSERT_EQUAL_UINT32(6, plan->steps.size());  // down/up for each of 3 characters
  TEST_ASSERT_EQUAL_HEX8(0x02, plan->steps[0].mods);

  for (const char* bad : {"{\"text\":\"é\"}", "{\"text\":\"😀\"}", "{\"text\":\"a\\u0001b\"}", "{\"text\":\"\"}", "{}", "{\"text\":5}"}) {
    McpReply r = e.tool("type_text", bad);
    TEST_ASSERT_TRUE_MESSAGE(isError(r), bad);
    TEST_ASSERT_FALSE_MESSAGE(e.automations.tick(false).has_value(), bad);  // nothing was queued
  }
  TEST_ASSERT_TRUE(isError(e.tool("type_text", "{\"text\":\"" + std::string(201, 'a') + "\"}")));
  e.clock.mouse = false;
  TEST_ASSERT_TRUE(isError(e.tool("type_text", "{\"text\":\"a\"}")));
}

void test_press_key_tool() {
  Env e;
  McpReply ok = e.tool("press_key", "{\"key\":\"F15\"}");
  TEST_ASSERT_FALSE(isError(ok));
  auto plan = e.automations.tick(false);
  TEST_ASSERT_EQUAL_UINT32(2, plan->steps.size());
  TEST_ASSERT_EQUAL_HEX8(0x6A, plan->steps[0].key);

  TEST_ASSERT_FALSE(isError(e.tool("press_key", "{\"key\":\"Ctrl+c\",\"times\":3}")));
  plan = e.automations.tick(false);
  size_t downs = 0;
  for (const Step& s : plan->steps) downs += s.kind == Step::Kind::KeyDown;
  TEST_ASSERT_EQUAL_UINT32(3, downs);

  for (const char* bad : {"{\"key\":\"Hyper\"}", "{}", "{\"key\":5}", "{\"key\":\"a\",\"times\":0}", "{\"key\":\"a\",\"times\":21}", "{\"key\":\"a\",\"times\":1.5}"})
    TEST_ASSERT_TRUE_MESSAGE(isError(e.tool("press_key", bad)), bad);
}

void test_network_tools_validate() {  // RF5
  Env e;
  McpReply g = e.tool("get_network");
  TEST_ASSERT_EQUAL_STRING("auto", toolText(g)["mode"].as<const char*>());
  McpReply ok = e.tool("set_network", "{\"mode\":\"ap\",\"apPassword\":\"supersegreta\"}");
  TEST_ASSERT_FALSE(isError(ok));
  TEST_ASSERT_TRUE(e.net.stored.mode == NetworkMode::Ap);
  McpReply bad = e.tool("set_network", "{\"apPassword\":\"short\"}");
  TEST_ASSERT_TRUE(isError(bad));
  TEST_ASSERT_EQUAL_STRING("apPassword", toolText(bad)["field"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("supersegreta", e.net.stored.apPassword.c_str());
  TEST_ASSERT_TRUE(isError(e.tool("set_network", "{\"mode\":\"wifi\"}")));
  TEST_ASSERT_EQUAL_INT(1, e.net.saves);
}

void test_bluetooth_tools() {
  Env e;
  McpReply g = e.tool("get_bluetooth");
  TEST_ASSERT_FALSE(isError(g));
  JsonDocument gd = toolText(g);
  TEST_ASSERT_FALSE(gd["connected"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("aabbccddee01", gd["peers"][0]["id"].as<const char*>());

  McpReply p = e.tool("start_pairing", "{\"seconds\":45}");
  TEST_ASSERT_FALSE(isError(p));
  TEST_ASSERT_EQUAL_UINT32(45000, toolText(p)["pairing"]["remainingMs"].as<uint32_t>());
  TEST_ASSERT_TRUE(e.link.pairingOpen);
  TEST_ASSERT_FALSE(isError(e.tool("start_pairing")));  // default window
  TEST_ASSERT_TRUE(isError(e.tool("start_pairing", "{\"seconds\":5}")));
  TEST_ASSERT_TRUE(isError(e.tool("start_pairing", "{\"seconds\":\"x\"}")));

  TEST_ASSERT_TRUE(toolText(e.tool("disconnect_bluetooth"))["held"].as<bool>());
  TEST_ASSERT_EQUAL_INT(1, e.link.disconnects);
  TEST_ASSERT_FALSE(toolText(e.tool("reconnect_bluetooth"))["held"].as<bool>());

  TEST_ASSERT_TRUE(isError(e.tool("forget_computer", "{}")));
  TEST_ASSERT_TRUE(isError(e.tool("forget_computer", "{\"id\":\"000000000000\"}")));
  McpReply f = e.tool("forget_computer", "{\"id\":\"aabbccddee01\"}");
  TEST_ASSERT_FALSE(isError(f));
  TEST_ASSERT_FALSE(toolText(f)["pairing"]["open"].as<bool>());
}

void test_restart_tool_calls_callback() {
  Env e;
  McpReply r = e.tool("restart");
  TEST_ASSERT_FALSE(isError(r));
  TEST_ASSERT_TRUE(toolText(r)["restarting"].as<bool>());
  TEST_ASSERT_EQUAL_INT(1, e.restarts);
}

void test_existing_tools_still_work_through_ex() {
  Env e;
  McpReply r = e.tool("get_status");
  TEST_ASSERT_FALSE(isError(r));
  TEST_ASSERT_EQUAL_STRING("running", toolText(r)["state"].as<const char*>());
  McpReply s = e.tool("set_config", "{\"amplitudePx\":200}");
  TEST_ASSERT_FALSE(isError(s));
  TEST_ASSERT_EQUAL_UINT16(200, e.controller.status().config.amplitudePx);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_tools_list_includes_new_tools);
  RUN_TEST(test_tools_without_services_are_not_listed);
  RUN_TEST(test_schedule_tools);
  RUN_TEST(test_legacy_mcp_matches_ex_without_services);
  RUN_TEST(test_devices_tools);
  RUN_TEST(test_script_tools_roundtrip);
  RUN_TEST(test_run_script_errors);
  RUN_TEST(test_automation_tools);
  RUN_TEST(test_type_text_tool);
  RUN_TEST(test_press_key_tool);
  RUN_TEST(test_network_tools_validate);
  RUN_TEST(test_bluetooth_tools);
  RUN_TEST(test_restart_tool_calls_callback);
  RUN_TEST(test_existing_tools_still_work_through_ex);
  return UNITY_END();
}
