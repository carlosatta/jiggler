#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "controller.h"
#include "fakes.h"
#include "mcp_server.h"

using namespace jig;

void setUp() {}
void tearDown() {}

namespace {

struct Mcp {
  FakeClock clock;
  XorShiftRng rng{1};
  MemStore store;
  Controller c{clock, rng, store};

  Mcp() { c.begin(); }

  McpReply call(const std::string& body) { return handleMcp(c, body); }

  McpReply tool(const std::string& name, const std::string& argsJson = "{}") {
    return call("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{\"name\":\"" + name +
                "\",\"arguments\":" + argsJson + "}}");
  }
};

JsonDocument parse(const std::string& s) {
  JsonDocument doc;
  TEST_ASSERT_TRUE_MESSAGE(!deserializeJson(doc, s), s.c_str());
  return doc;
}

// Parses the JSON text carried in result.content[0].text.
JsonDocument toolText(const McpReply& r) {
  JsonDocument reply = parse(r.body);
  TEST_ASSERT_EQUAL_STRING("text", reply["result"]["content"][0]["type"].as<const char*>());
  return parse(reply["result"]["content"][0]["text"].as<std::string>());
}

bool isError(const McpReply& r) {
  JsonDocument reply = parse(r.body);
  TEST_ASSERT_TRUE(reply["result"]["isError"].is<bool>());
  return reply["result"]["isError"].as<bool>();
}

int errorCode(const McpReply& r) { return parse(r.body)["error"]["code"].as<int>(); }

}  // namespace

void test_initialize() {
  Mcp m;
  McpReply r = m.call(
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-03-26\","
      "\"capabilities\":{},\"clientInfo\":{\"name\":\"t\",\"version\":\"1\"}}}");
  TEST_ASSERT_EQUAL_INT(200, r.httpStatus);
  JsonDocument d = parse(r.body);
  TEST_ASSERT_EQUAL_STRING("2.0", d["jsonrpc"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(1, d["id"].as<int>());
  TEST_ASSERT_EQUAL_STRING("2025-03-26", d["result"]["protocolVersion"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("jiggler", d["result"]["serverInfo"]["name"].as<const char*>());
  TEST_ASSERT_TRUE(r.body.find("\"tools\":{}") != std::string::npos);
}

void test_string_id_is_echoed_and_ping() {
  Mcp m;
  McpReply r = m.call("{\"jsonrpc\":\"2.0\",\"id\":\"abc\",\"method\":\"ping\"}");
  TEST_ASSERT_EQUAL_INT(200, r.httpStatus);
  JsonDocument d = parse(r.body);
  TEST_ASSERT_EQUAL_STRING("abc", d["id"].as<const char*>());
  TEST_ASSERT_TRUE(r.body.find("\"result\":{}") != std::string::npos);
}

void test_tools_list_names() {
  Mcp m;
  McpReply r = m.call("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}");
  JsonDocument d = parse(r.body);
  JsonArray tools = d["result"]["tools"].as<JsonArray>();
  const char* expected[] = {"get_status", "start", "pause", "stop", "move_now", "set_config"};
  TEST_ASSERT_EQUAL_UINT32(6, tools.size());
  for (size_t i = 0; i < 6; i++) {
    TEST_ASSERT_EQUAL_STRING(expected[i], tools[i]["name"].as<const char*>());
    TEST_ASSERT_TRUE(tools[i]["description"].is<const char*>());
    TEST_ASSERT_TRUE(tools[i]["inputSchema"].is<JsonObject>());
  }
}

void test_call_get_status() {
  Mcp m;
  McpReply r = m.tool("get_status");
  TEST_ASSERT_EQUAL_INT(200, r.httpStatus);
  TEST_ASSERT_FALSE(isError(r));
  TEST_ASSERT_EQUAL_STRING("running", toolText(r)["state"].as<const char*>());
}

void test_call_state_tools() {
  Mcp m;
  TEST_ASSERT_EQUAL_STRING("paused", toolText(m.tool("pause"))["state"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("running", toolText(m.tool("start"))["state"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("stopped", toolText(m.tool("stop"))["state"].as<const char*>());
}

void test_start_tool_accepts_minutes() {
  Mcp m;
  m.tool("stop");
  McpReply r = m.tool("start", "{\"minutes\":10}");
  TEST_ASSERT_FALSE(isError(r));
  TEST_ASSERT_EQUAL_UINT32(600000, toolText(r)["stopsInMs"].as<uint32_t>());
  TEST_ASSERT_TRUE(isError(m.tool("start", "{\"minutes\":5000}")));
  TEST_ASSERT_TRUE(isError(m.tool("start", "{\"minutes\":\"x\"}")));
}

void test_start_tool_refuses_without_a_paired_computer() {
  Mcp m;
  m.clock.paired = false;
  m.tool("stop");
  McpReply r = m.tool("start");
  TEST_ASSERT_TRUE(isError(r));
  TEST_ASSERT_TRUE(isError(m.tool("start", "{\"minutes\":10}")));
}

void test_call_move_now() {
  Mcp m;
  McpReply ok = m.tool("move_now");
  TEST_ASSERT_FALSE(isError(ok));
  TEST_ASSERT_TRUE(toolText(ok)["queued"].as<bool>());
  TEST_ASSERT_TRUE(m.c.tick().has_value());

  m.clock.mouse = false;
  McpReply bad = m.tool("move_now");
  TEST_ASSERT_EQUAL_INT(200, bad.httpStatus);
  TEST_ASSERT_TRUE(isError(bad));
  TEST_ASSERT_EQUAL_STRING("mouse_not_connected", toolText(bad)["error"].as<const char*>());
}

void test_call_set_config_valid_and_invalid() {  // RF2
  Mcp m;
  McpReply ok = m.tool("set_config", "{\"amplitudePx\":200}");
  TEST_ASSERT_FALSE(isError(ok));
  TEST_ASSERT_EQUAL_INT(200, toolText(ok)["amplitudePx"].as<int>());
  TEST_ASSERT_EQUAL_INT(1, m.store.saves);

  McpReply bad = m.tool("set_config", "{\"maxIntervalSec\":10}");
  TEST_ASSERT_TRUE(isError(bad));
  JsonDocument t = toolText(bad);
  TEST_ASSERT_EQUAL_STRING("invalid_config", t["error"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("maxIntervalSec", t["field"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(1, m.store.saves);
  TEST_ASSERT_EQUAL_UINT32(120, m.c.status().config.maxIntervalSec);
}

void test_call_set_config_wrong_type_is_error_result() {  // RF3
  Mcp m;
  McpReply r = m.tool("set_config", "{\"amplitudePx\":\"big\"}");
  TEST_ASSERT_EQUAL_INT(200, r.httpStatus);
  TEST_ASSERT_TRUE(isError(r));
  TEST_ASSERT_EQUAL_STRING("amplitudePx", toolText(r)["field"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(0, m.store.saves);
}

void test_notification_returns_202_empty() {  // RF5
  Mcp m;
  McpReply a = m.call("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}");
  TEST_ASSERT_EQUAL_INT(202, a.httpStatus);
  TEST_ASSERT_EQUAL_STRING("", a.body.c_str());
  McpReply b = m.call("{\"jsonrpc\":\"2.0\",\"method\":\"something/unknown\"}");
  TEST_ASSERT_EQUAL_INT(202, b.httpStatus);
  TEST_ASSERT_EQUAL_STRING("", b.body.c_str());
}

void test_protocol_errors() {  // RF3, RF5
  const std::string call = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":";
  const struct {
    std::string body;
    int http;
    int code;
  } cases[] = {
      {"not json", 400, -32700},
      {"", 400, -32700},
      {"[]", 200, -32600},
      {"[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}]", 200, -32600},
      {"\"str\"", 200, -32600},
      {"{\"jsonrpc\":\"2.0\",\"id\":1}", 200, -32600},
      {"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":5}", 200, -32600},
      {"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"nope\"}", 200, -32601},
      {call + "{\"name\":\"nope\"}}", 200, -32602},
      {call + "{\"name\":\"get_status\",\"arguments\":\"x\"}}", 200, -32602},
      {call + "{\"arguments\":{}}}", 200, -32602},
      {call + "\"x\"}", 200, -32602},
      {call + "{\"name\":5}}", 200, -32602},
  };
  for (const auto& tc : cases) {
    Mcp m;
    McpReply r = m.call(tc.body);
    TEST_ASSERT_EQUAL_INT_MESSAGE(tc.http, r.httpStatus, tc.body.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(tc.code, errorCode(r), tc.body.c_str());
  }
}

void test_parse_error_reply_has_null_id() {
  Mcp m;
  McpReply r = m.call("not json");
  TEST_ASSERT_TRUE(r.body.find("\"id\":null") != std::string::npos);
}

void test_oversized_body_rejected() {  // RF3
  Mcp m;
  const std::string big = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\",\"pad\":\"" + std::string(4200, 'a') + "\"}";
  McpReply r = m.call(big);
  TEST_ASSERT_EQUAL_INT(413, r.httpStatus);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_initialize);
  RUN_TEST(test_string_id_is_echoed_and_ping);
  RUN_TEST(test_tools_list_names);
  RUN_TEST(test_call_get_status);
  RUN_TEST(test_call_state_tools);
  RUN_TEST(test_call_move_now);
  RUN_TEST(test_call_set_config_valid_and_invalid);
  RUN_TEST(test_call_set_config_wrong_type_is_error_result);
  RUN_TEST(test_notification_returns_202_empty);
  RUN_TEST(test_protocol_errors);
  RUN_TEST(test_parse_error_reply_has_null_id);
  RUN_TEST(test_oversized_body_rejected);
  RUN_TEST(test_start_tool_accepts_minutes);
  RUN_TEST(test_start_tool_refuses_without_a_paired_computer);
  return UNITY_END();
}
