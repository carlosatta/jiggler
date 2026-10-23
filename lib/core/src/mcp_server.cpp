#include "mcp_server.h"

#include <ArduinoJson.h>

#include "api_router.h"
#include "json_codec.h"

namespace jig {

namespace {

constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;

const char* const kToolNames[] = {"get_status", "start", "pause", "stop", "move_now", "set_config"};

std::string serialize(const JsonDocument& doc) {
  std::string out;
  serializeJson(doc, out);
  return out;
}

McpReply replyError(JsonVariantConst id, int code, const char* message, int http = 200) {
  JsonDocument doc;
  doc["jsonrpc"] = "2.0";
  doc["id"] = id;
  doc["error"]["code"] = code;
  doc["error"]["message"] = message;
  return {http, serialize(doc)};
}

void addTool(JsonArray tools, const char* name, const char* description, JsonObject* schemaOut) {
  JsonObject tool = tools.add<JsonObject>();
  tool["name"] = name;
  tool["description"] = description;
  JsonObject schema = tool["inputSchema"].to<JsonObject>();
  schema["type"] = "object";
  *schemaOut = schema;
}

void addNumberProp(JsonObject props, const char* name, const char* type, double min, double max,
                   const char* description) {
  JsonObject p = props[name].to<JsonObject>();
  p["type"] = type;
  p["minimum"] = min;
  p["maximum"] = max;
  p["description"] = description;
}

void fillTools(JsonArray tools) {
  JsonObject schema;
  addTool(tools, "get_status",
          "Get the jiggler status: run state, mouse/WiFi connection, time to next movement, movement count, last movement, config.",
          &schema);
  schema["properties"].to<JsonObject>();
  addTool(tools, "start", "Start (or resume) the jiggler.", &schema);
  schema["properties"].to<JsonObject>();
  addTool(tools, "pause", "Pause the jiggler; resuming keeps the remaining time.", &schema);
  schema["properties"].to<JsonObject>();
  addTool(tools, "stop", "Stop the jiggler completely.", &schema);
  schema["properties"].to<JsonObject>();
  addTool(tools, "move_now", "Move the mouse now (a test movement). Fails if the mouse is not connected.", &schema);
  schema["properties"].to<JsonObject>();
  addTool(tools, "set_config",
          "Change configuration. Every field is optional; omitted fields keep their value.", &schema);
  JsonObject props = schema["properties"].to<JsonObject>();
  addNumberProp(props, "minIntervalSec", "integer", 5, 3600, "Minimum seconds between movements.");
  addNumberProp(props, "maxIntervalSec", "integer", 5, 3600, "Maximum seconds between movements (>= min).");
  addNumberProp(props, "amplitudePx", "integer", 5, 600, "Maximum movement distance in pixels.");
  addNumberProp(props, "clickProbability", "number", 0, 1, "Probability of a click per movement.");
  addNumberProp(props, "scrollProbability", "number", 0, 1, "Probability of a scroll per movement.");
  schema["additionalProperties"] = false;
}

struct ToolOutput {
  std::string text;
  bool isError;
};

ToolOutput callTool(Controller& c, const std::string& name, JsonVariantConst args) {
  if (name == "get_status") return {statusToJson(c.status()), false};
  if (name == "start") c.start();
  else if (name == "pause") c.pause();
  else if (name == "stop") c.stop();
  else if (name == "move_now") {
    CmdResult r = c.moveNow();
    if (!r.ok) return {errorJson(r.error, r.field, r.message), true};
    return {"{\"queued\":true}", false};
  } else {  // set_config
    ConfigPatch patch;
    ParseError perr;
    if (!args.isNull() && !parseConfigPatch(args.as<JsonObjectConst>(), patch, perr))
      return {errorJson(perr.code, perr.field, perr.message), true};
    CmdResult r = c.updateConfig(patch);
    if (!r.ok) return {errorJson(r.error, r.field, r.message), true};
    return {configToJson(c.status().config), false};
  }
  return {statusToJson(c.status()), false};
}

bool isKnownTool(const std::string& name) {
  for (const char* known : kToolNames)
    if (name == known) return true;
  return false;
}

McpReply handleToolsCall(Controller& c, JsonVariantConst id, JsonVariantConst params) {
  if (!params.is<JsonObjectConst>()) return replyError(id, kInvalidParams, "params must be an object");
  if (!params["name"].is<const char*>()) return replyError(id, kInvalidParams, "missing tool name");
  const std::string name = params["name"].as<std::string>();
  if (!isKnownTool(name)) return replyError(id, kInvalidParams, "unknown tool");
  JsonVariantConst args = params["arguments"];
  if (!args.isNull() && !args.is<JsonObjectConst>())
    return replyError(id, kInvalidParams, "arguments must be an object");

  ToolOutput out = callTool(c, name, args);
  JsonDocument doc;
  doc["jsonrpc"] = "2.0";
  doc["id"] = id;
  JsonObject content = doc["result"]["content"].add<JsonObject>();
  content["type"] = "text";
  content["text"] = out.text;
  doc["result"]["isError"] = out.isError;
  return {200, serialize(doc)};
}

}  // namespace

McpReply handleMcp(Controller& c, const std::string& body) {
  if (body.size() > kMaxBodyBytes) return {413, errorJson("payload_too_large")};

  JsonDocument doc;
  if (deserializeJson(doc, body)) return replyError(JsonVariantConst(), kParseError, "parse error", 400);
  if (!doc.is<JsonObject>()) return replyError(JsonVariantConst(), kInvalidRequest, "invalid request");

  JsonObjectConst req = doc.as<JsonObjectConst>();
  JsonVariantConst id = req["id"];
  if (!req["method"].is<const char*>()) return replyError(id, kInvalidRequest, "missing method");
  if (id.isNull()) return {202, ""};  // notification: no reply

  const std::string method = req["method"].as<std::string>();
  JsonDocument reply;
  reply["jsonrpc"] = "2.0";
  reply["id"] = id;

  if (method == "initialize") {
    JsonObject result = reply["result"].to<JsonObject>();
    result["protocolVersion"] = "2025-03-26";
    result["capabilities"]["tools"].to<JsonObject>();
    result["serverInfo"]["name"] = "jiggler";
    result["serverInfo"]["version"] = "1.0.0";
  } else if (method == "ping") {
    reply["result"].to<JsonObject>();
  } else if (method == "tools/list") {
    fillTools(reply["result"]["tools"].to<JsonArray>());
  } else if (method == "tools/call") {
    return handleToolsCall(c, id, req["params"]);
  } else {
    return replyError(id, kMethodNotFound, "method not found");
  }
  return {200, serialize(reply)};
}

}  // namespace jig
