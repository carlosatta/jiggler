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

const char* const kToolNames[] = {"get_status", "start", "pause", "resume", "stop", "move_now", "set_config"};

std::string serialize(JsonVariantConst doc) {
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

void addProp(JsonObject props, const char* name, const char* type, const char* description) {
  JsonObject p = props[name].to<JsonObject>();
  p["type"] = type;
  p["description"] = description;
}

void fillBaseTools(JsonArray tools) {
  JsonObject schema;
  addTool(tools, "get_status",
          "Get the jiggler status: run state, mouse/WiFi connection, time to next movement, movement count, last movement, config.",
          &schema);
  schema["properties"].to<JsonObject>();
  addTool(tools, "start",
          "Start (or resume) the jiggler. With `minutes` it stops by itself after that long (1-1440); without it, it runs until stopped.",
          &schema);
  addNumberProp(schema["properties"].to<JsonObject>(), "minutes", "integer", 1, 1440,
                "Stop by itself after this many minutes (optional).");
  addTool(tools, "pause",
          "Pause the jiggler. With `minutes` it resumes by itself after that long (1-1440); without it, it stays paused until `resume`. A time limit set with `start` keeps counting while paused.",
          &schema);
  addNumberProp(schema["properties"].to<JsonObject>(), "minutes", "integer", 1, 1440,
                "Resume by itself after this many minutes (optional).");
  addTool(tools, "resume", "End a pause now. Fails if the jiggler is not paused.", &schema);
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

// ---- extended tools --------------------------------------------------------------------------

struct ExtTool {
  const char* name;
  bool (*available)(const ApiContext&);
};

bool hasAppearance(const ApiContext& c) { return c.appearance != nullptr; }
bool hasScripts(const ApiContext& c) { return c.scripts != nullptr; }
bool hasAutomations(const ApiContext& c) { return c.automations != nullptr; }
bool hasNetwork(const ApiContext& c) { return c.network != nullptr; }
bool hasRestart(const ApiContext& c) { return static_cast<bool>(c.restart); }
bool hasBluetooth(const ApiContext& c) { return c.bluetooth != nullptr; }
bool hasSchedules(const ApiContext& c) { return c.schedules != nullptr; }
bool hasClock(const ApiContext& c) { return c.clock != nullptr; }
bool hasButton(const ApiContext& c) { return c.button != nullptr; }
bool hasUpdates(const ApiContext& c) { return c.updates != nullptr; }

const ExtTool kExtTools[] = {
    {"get_appearance", hasAppearance}, {"set_appearance", hasAppearance},
    {"list_scripts", hasScripts},    {"save_script", hasScripts},         {"delete_script", hasScripts},
    {"run_script", hasAutomations},  {"get_automations", hasAutomations}, {"set_automations", hasAutomations},
    {"press_key", hasAutomations},   {"type_text", hasAutomations},       {"get_network", hasNetwork},
    {"set_network", hasNetwork},     {"restart", hasRestart},
    {"get_bluetooth", hasBluetooth}, {"start_pairing", hasBluetooth},     {"disable_bluetooth", hasBluetooth},
    {"enable_bluetooth", hasBluetooth}, {"set_bluetooth_settings", hasBluetooth}, {"forget_computer", hasBluetooth},
    {"get_schedules", hasSchedules}, {"set_schedules", hasSchedules},
    {"get_time", hasClock},          {"set_time", hasClock},
    {"get_button", hasButton},       {"set_button", hasButton},
    {"get_updates", hasUpdates},     {"set_updates", hasUpdates},    {"check_updates", hasUpdates}, {"install_update", hasUpdates},
};

const ExtTool* findExtTool(const ApiContext& ctx, const std::string& name) {
  for (const ExtTool& t : kExtTools)
    if (name == t.name && t.available(ctx)) return &t;
  return nullptr;
}

void fillExtTools(JsonArray tools, const ApiContext& ctx) {
  for (const ExtTool& t : kExtTools) {
    if (!t.available(ctx)) continue;
    const std::string name = t.name;
    JsonObject schema;
    if (name == "get_appearance") {
      addTool(tools, t.name, "Get how the device appears to computers over Bluetooth: the chosen model (or custom), any overrides, the name currently shown, and the models you can choose.", &schema);
      schema["properties"].to<JsonObject>();
    } else if (name == "set_appearance") {
      addTool(tools, t.name,
              "Change how the device appears over Bluetooth (a full replacement: omitted fields go back to default). Takes effect after a restart, and every paired computer must forget the device and pair again.",
              &schema);
      JsonObject p = schema["properties"].to<JsonObject>();
      addProp(p, "profile", "string", "A model id from get_appearance, or custom (default).");
      addProp(p, "name", "string", "The Bluetooth name (1-29 characters).");
      addProp(p, "manufacturer", "string", "Override the manufacturer (1-32 characters).");
      addNumberProp(p, "vid", "integer", 0, 65535, "Override the vendor id.");
      addNumberProp(p, "pid", "integer", 0, 65535, "Override the product id.");
      addProp(p, "mac", "string", "Override the Bluetooth address, AA:BB:CC:DD:EE:FF, unicast.");
    } else if (name == "list_scripts") {
      addTool(tools, t.name, "List the stored scripts (the built-in keepalive-key plus user scripts).", &schema);
      schema["properties"].to<JsonObject>();
    } else if (name == "save_script") {
      addTool(tools, t.name,
              "Create or overwrite a script. Steps: move{dx,dy}, click, scroll{notches}, key{key}, type{text}, wait{ms} or wait{minMs,maxMs}, repeat{times,steps}. Keys look like F15 or Ctrl+Alt+Delete; text is printable ASCII (US layout).",
              &schema);
      JsonObject p = schema["properties"].to<JsonObject>();
      addProp(p, "name", "string", "Script name, a-z 0-9 _ -, up to 24 characters.");
      JsonObject script = p["script"].to<JsonObject>();
      script["type"] = "object";
      script["description"] = "{\"steps\":[...]}";
      schema["required"].add("name");
      schema["required"].add("script");
    } else if (name == "delete_script") {
      addTool(tools, t.name, "Delete a user script.", &schema);
      addProp(schema["properties"].to<JsonObject>(), "name", "string", "Script name.");
      schema["required"].add("name");
    } else if (name == "run_script") {
      addTool(tools, t.name, "Run a stored script now. Fails if the mouse/keyboard is not connected to the computer.", &schema);
      addProp(schema["properties"].to<JsonObject>(), "name", "string", "Script name.");
      schema["required"].add("name");
    } else if (name == "get_automations") {
      addTool(tools, t.name, "Get the scheduled automations (a script run at random intervals).", &schema);
      schema["properties"].to<JsonObject>();
    } else if (name == "set_automations") {
      addTool(tools, t.name, "Replace the automations (max 4). They run only while the jiggler is started.", &schema);
      JsonObject items = schema["properties"]["automations"].to<JsonObject>();
      items["type"] = "array";
      JsonObject item = items["items"].to<JsonObject>();
      item["type"] = "object";
      JsonObject p = item["properties"].to<JsonObject>();
      addProp(p, "id", "string", "Unique id.");
      addProp(p, "script", "string", "Script name to run.");
      addNumberProp(p, "minIntervalSec", "integer", 5, 86400, "Minimum seconds between runs.");
      addNumberProp(p, "maxIntervalSec", "integer", 5, 86400, "Maximum seconds between runs (>= min).");
      addProp(p, "enabled", "boolean", "Whether it runs.");
      schema["required"].add("automations");
    } else if (name == "get_schedules") {
      addTool(tools, t.name, "Get the weekly schedules: windows when the jiggler starts and stops by itself, and actions done at one time (start, stop, pause, resume, Bluetooth on/off, discovery).", &schema);
      schema["properties"].to<JsonObject>();
    } else if (name == "set_schedules") {
      addTool(tools, t.name,
              "Replace the weekly schedules (max 8). A window (kind window) runs the jiggler from its start time for durationMin minutes. An action (kind action) does one thing at its start time. jitterMin shifts every time by a random +/- number of minutes, drawn afresh each day. A window that is open when you save it starts at once. Needs the clock (get_time).",
              &schema);
      JsonObject props = schema["properties"].to<JsonObject>();
      JsonObject items = props["schedules"].to<JsonObject>();
      items["type"] = "array";
      JsonObject item = items["items"].to<JsonObject>();
      item["type"] = "object";
      JsonObject p = item["properties"].to<JsonObject>();
      addProp(p, "id", "string", "Unique id (a-z 0-9 _ -).");
      addProp(p, "enabled", "boolean", "Whether the schedule is active (default true).");
      addProp(p, "kind", "string", "window (default) or action.");
      addProp(p, "action", "string", "For an action: start, stop, pause, resume, bluetooth_on, bluetooth_off or discovery.");
      JsonObject days = p["days"].to<JsonObject>();
      days["type"] = "array";
      days["items"]["type"] = "string";
      days["description"] = "Days it applies to: mon, tue, wed, thu, fri, sat, sun.";
      addProp(p, "start", "string", "Start time HH:MM in the device timezone, e.g. 09:00. For an action, the time it happens.");
      addNumberProp(p, "durationMin", "integer", 1, 1440, "A window's length in minutes. It may run past midnight.");
      addNumberProp(p, "jitterMin", "integer", 0, 120, "Random shift of each time, plus or minus this many minutes (default 0).");
      addNumberProp(p, "times", "integer", 1, 999, "Run this many times and then turn the schedule off. Omit or null for no limit (every week).");
      addNumberProp(p, "remaining", "integer", 0, 999, "Runs left when times is set (read it from get_schedules; omit to start a fresh run).");
      schema["required"].add("schedules");
    } else if (name == "get_time") {
      addTool(tools, t.name, "Get the device clock (whether it is set, from NTP or by hand, and the local time) with its timezone and NTP server.", &schema);
      schema["properties"].to<JsonObject>();
    } else if (name == "set_time") {
      addTool(tools, t.name, "Change the timezone or NTP server, or set the clock by hand (a device without internet loses the time at every restart).", &schema);
      JsonObject p = schema["properties"].to<JsonObject>();
      addProp(p, "timezone", "string", "Timezone name, e.g. Europe/Rome, America/New_York, UTC (see get_time for the list).");
      addProp(p, "ntpServer", "string", "NTP server host name, e.g. pool.ntp.org; empty to stop using NTP.");
      addNumberProp(p, "epoch", "integer", 1700000000, 4102444800, "Set the clock to this Unix time in seconds.");
    } else if (name == "get_updates") {
      addTool(tools, t.name, "Get the firmware update state: the running version, the latest release found, the mode (manual, automatic, ignore) and the repository it looks at.", &schema);
      schema["properties"].to<JsonObject>();
    } else if (name == "set_updates") {
      addTool(tools, t.name, "Choose how updates are handled and where they come from. manual: it looks for releases and tells you; automatic: it also installs them; ignore: it never looks by itself.", &schema);
      JsonObject p = schema["properties"].to<JsonObject>();
      addProp(p, "mode", "string", "manual, automatic or ignore.");
      addProp(p, "repoUrl", "string", "A GitHub repository address, https://github.com/owner/repo; empty for the project's own repository.");
      addProp(p, "checkTime", "string", "The time of day (device timezone, HH:MM, default 03:00) when it looks for a release and, in automatic mode, installs it.");
    } else if (name == "check_updates") {
      addTool(tools, t.name, "Look for a newer release now (takes a few seconds; read the result with get_updates).", &schema);
      schema["properties"].to<JsonObject>();
    } else if (name == "install_update") {
      addTool(tools, t.name, "Install the newer release found by check_updates. The device downloads it, then restarts.", &schema);
      schema["properties"].to<JsonObject>();
    } else if (name == "get_button") {
      addTool(tools, t.name, "Get which GPIO the physical button is wired to (default 0, the BOOT button) and the pins that can be used. A tap starts or stops the jiggler, a 5 to 10 s hold opens Discovery, a 30 s hold switches the network mode.", &schema);
      schema["properties"].to<JsonObject>();
    } else if (name == "set_button") {
      addTool(tools, t.name, "Choose the GPIO of the physical button (wired to ground).", &schema);
      addNumberProp(schema["properties"].to<JsonObject>(), "gpio", "integer", 0, 33, "A pin from get_button.");
      schema["required"].add("gpio");
    } else if (name == "press_key") {
      addTool(tools, t.name, "Press a key (or combination such as Ctrl+C) on the keyboard now.", &schema);
      JsonObject p = schema["properties"].to<JsonObject>();
      addProp(p, "key", "string", "Key name, e.g. F15, Enter, a, Ctrl+Alt+Delete.");
      addNumberProp(p, "times", "integer", 1, 20, "How many times (default 1).");
      schema["required"].add("key");
    } else if (name == "type_text") {
      addTool(tools, t.name, "Type text on the keyboard now (printable ASCII, newline and tab; US layout).", &schema);
      addProp(schema["properties"].to<JsonObject>(), "text", "string", "Text to type, up to 200 characters.");
      schema["required"].add("text");
    } else if (name == "get_network") {
      addTool(tools, t.name, "Get the network mode (auto or ap) and the access-point settings.", &schema);
      schema["properties"].to<JsonObject>();
    } else if (name == "set_network") {
      addTool(tools, t.name, "Change the network settings. Takes effect after a restart.", &schema);
      JsonObject p = schema["properties"].to<JsonObject>();
      JsonObject mode = p["mode"].to<JsonObject>();
      mode["type"] = "string";
      mode["enum"].add("auto");
      mode["enum"].add("ap");
      addProp(p, "apSsid", "string", "Access-point name (1-32 characters).");
      addProp(p, "apPassword", "string", "Access-point password (8-63 characters) or empty to generate one.");
    } else if (name == "restart") {
      addTool(tools, t.name, "Restart the device (needed after changing the appearance or network).", &schema);
      schema["properties"].to<JsonObject>();
    } else if (name == "get_bluetooth") {
      addTool(tools, t.name, "Get the Bluetooth state (off, waiting, discovery or connected), which computer is connected and for how long, the discovery time, and the paired computers.", &schema);
      schema["properties"].to<JsonObject>();
    } else if (name == "start_pairing") {
      addTool(tools, t.name, "Open Discovery (the pairing window) so a new computer can pair; it also switches Bluetooth on. While it is closed, new computers are refused.", &schema);
      addNumberProp(schema["properties"].to<JsonObject>(), "seconds", "integer", 30, 600, "How long the window stays open (default: the configured discovery time, 180 s).");
    } else if (name == "disable_bluetooth") {
      addTool(tools, t.name, "Switch Bluetooth off: the computer is disconnected and the device is invisible until enable_bluetooth (it keeps its pairings).", &schema);
      schema["properties"].to<JsonObject>();
    } else if (name == "enable_bluetooth") {
      addTool(tools, t.name, "Switch Bluetooth on: paired computers can find and reconnect to the device.", &schema);
      schema["properties"].to<JsonObject>();
    } else if (name == "set_bluetooth_settings") {
      addTool(tools, t.name, "Change the Bluetooth settings.", &schema);
      JsonObject p = schema["properties"].to<JsonObject>();
      addNumberProp(p, "discoverySeconds", "integer", 30, 600, "How long Discovery stays open (default 180).");
      addProp(p, "offWhenComputerDisconnects", "boolean", "Switch Bluetooth off when the computer itself ends the connection (default true).");
    } else if (name == "forget_computer") {
      addTool(tools, t.name, "Forget one paired computer (it will have to pair again). Use the id from get_bluetooth.", &schema);
      addProp(schema["properties"].to<JsonObject>(), "id", "string", "The computer id from get_bluetooth.");
      schema["required"].add("id");
    }
  }
}

struct ToolOutput {
  std::string text;
  bool isError;
};

ToolOutput viaRoute(ApiContext& ctx, const char* method, const std::string& path, const std::string& body = "") {
  HttpResponse r = handleApiEx(ctx, method, path, body);
  return {r.body, r.status >= 400};
}

ToolOutput argError(const char* field, const char* message) {
  return {errorJson("invalid_arguments", field, message), true};
}

std::string nameArg(JsonVariantConst args) {
  return args["name"].is<const char*>() ? args["name"].as<std::string>() : std::string();
}

ToolOutput fromCmd(const CmdResult& r, const std::string& okText) {
  if (r.ok) return {okText, false};
  return {errorJson(r.error, r.field, r.message), true};
}

ToolOutput wrapArray(ApiContext& ctx, const char* key, const char* path, JsonVariantConst args) {
  JsonDocument doc;
  doc[key] = args[key];
  return viaRoute(ctx, "PUT", path, serialize(doc));
}

ToolOutput callExtTool(ApiContext& ctx, const std::string& name, JsonVariantConst args) {
  if (name == "get_appearance") return viaRoute(ctx, "GET", "/api/appearance");
  if (name == "set_appearance") return viaRoute(ctx, "PUT", "/api/appearance", args.isNull() ? "{}" : serialize(args));
  if (name == "list_scripts") return viaRoute(ctx, "GET", "/api/scripts");
  if (name == "get_automations") return viaRoute(ctx, "GET", "/api/automations");
  if (name == "set_automations") return wrapArray(ctx, "automations", "/api/automations", args);
  if (name == "get_schedules") return viaRoute(ctx, "GET", "/api/schedules");
  if (name == "set_schedules") return viaRoute(ctx, "PUT", "/api/schedules", args.isNull() ? "{}" : serialize(args));
  if (name == "get_time") return viaRoute(ctx, "GET", "/api/time");
  if (name == "set_time") {
    ToolOutput last = viaRoute(ctx, "GET", "/api/time");
    if (args["epoch"].is<int64_t>()) {
      JsonDocument doc;
      doc["epoch"] = args["epoch"];
      last = viaRoute(ctx, "POST", "/api/time/set", serialize(doc));
      if (last.isError) return last;
    } else if (!args["epoch"].isNull()) {
      return argError("epoch", "must be a Unix time in seconds");
    }
    JsonDocument settings;
    if (!args["timezone"].isNull()) settings["timezone"] = args["timezone"];
    if (!args["ntpServer"].isNull()) settings["ntpServer"] = args["ntpServer"];
    if (!settings.as<JsonObject>().isNull() && settings.size() > 0) last = viaRoute(ctx, "PUT", "/api/time", serialize(settings));
    return last;
  }
  if (name == "get_updates") return viaRoute(ctx, "GET", "/api/updates");
  if (name == "set_updates") return viaRoute(ctx, "PUT", "/api/updates", args.isNull() ? "{}" : serialize(args));
  if (name == "check_updates") return viaRoute(ctx, "POST", "/api/updates/check");
  if (name == "install_update") return viaRoute(ctx, "POST", "/api/updates/install");
  if (name == "get_button") return viaRoute(ctx, "GET", "/api/button");
  if (name == "set_button") return viaRoute(ctx, "PUT", "/api/button", args.isNull() ? "{}" : serialize(args));
  if (name == "get_network") return viaRoute(ctx, "GET", "/api/network");
  if (name == "restart") return viaRoute(ctx, "POST", "/api/restart");
  if (name == "get_bluetooth") return viaRoute(ctx, "GET", "/api/bluetooth");
  if (name == "start_pairing") return viaRoute(ctx, "POST", "/api/bluetooth/pairing", args.isNull() ? "" : serialize(args));
  if (name == "disable_bluetooth") return viaRoute(ctx, "POST", "/api/bluetooth/disable");
  if (name == "enable_bluetooth") return viaRoute(ctx, "POST", "/api/bluetooth/enable");
  if (name == "set_bluetooth_settings") return viaRoute(ctx, "PUT", "/api/bluetooth/settings", serialize(args));
  if (name == "forget_computer") {
    if (!args["id"].is<const char*>() || args["id"].as<std::string>().empty()) return argError("id", "is required");
    return viaRoute(ctx, "DELETE", "/api/bluetooth/peers/" + args["id"].as<std::string>());
  }
  if (name == "set_network") return viaRoute(ctx, "PUT", "/api/network", args.isNull() ? "{}" : serialize(args));

  if (name == "save_script") {
    const std::string script = nameArg(args);
    if (script.empty()) return argError("name", "is required");
    if (!args["script"].is<JsonObjectConst>()) return argError("script", "must be an object like {\"steps\":[...]}");
    return viaRoute(ctx, "PUT", "/api/scripts/" + script, serialize(args["script"]));
  }
  if (name == "delete_script" || name == "run_script") {
    const std::string script = nameArg(args);
    if (script.empty()) return argError("name", "is required");
    if (name == "delete_script") return viaRoute(ctx, "DELETE", "/api/scripts/" + script);
    return viaRoute(ctx, "POST", "/api/scripts/" + script + "/run");
  }

  JsonDocument script;
  if (name == "type_text") {
    if (!args["text"].is<const char*>()) return argError("text", "must be a string");
    JsonObject step = script["steps"].add<JsonObject>();
    step["op"] = "type";
    step["text"] = args["text"].as<std::string>();
  } else if (name == "press_key") {
    if (!args["key"].is<const char*>()) return argError("key", "must be a string");
    int64_t times = 1;
    if (!args["times"].isNull()) {
      if (!args["times"].is<int64_t>() || args["times"].as<int64_t>() < 1 || args["times"].as<int64_t>() > 20)
        return argError("times", "must be an integer from 1 to 20");
      times = args["times"].as<int64_t>();
    }
    JsonObject first = script["steps"].add<JsonObject>();
    if (times == 1) {
      first["op"] = "key";
      first["key"] = args["key"].as<std::string>();
    } else {
      first["op"] = "repeat";
      first["times"] = times;
      JsonObject press = first["steps"].add<JsonObject>();
      press["op"] = "key";
      press["key"] = args["key"].as<std::string>();
      JsonObject gap = first["steps"].add<JsonObject>();
      gap["op"] = "wait";
      gap["ms"] = 50;
    }
  } else {
    return argError("", "unknown tool");
  }
  return fromCmd(ctx.automations->runInline(serialize(script)), "{\"queued\":true}");
}

// ---- base tools ------------------------------------------------------------------------------

ToolOutput callBaseTool(Controller& c, const std::string& name, JsonVariantConst args) {
  if (name == "get_status") return {statusToJson(c.status()), false};
  if (name == "start") {
    std::optional<uint32_t> minutes;
    ParseError perr;
    if (!args.isNull() && !parseStartRequest(args.as<JsonObjectConst>(), minutes, perr))
      return {errorJson(perr.code, perr.field, perr.message), true};
    CmdResult started = minutes ? c.startFor(*minutes) : c.start();
    if (!started.ok) return {errorJson(started.error, started.field, started.message), true};
  } else if (name == "pause") {
    std::optional<uint32_t> minutes;
    ParseError perr;
    if (!args.isNull() && !parseStartRequest(args.as<JsonObjectConst>(), minutes, perr))
      return {errorJson(perr.code, perr.field, perr.message), true};
    if (minutes) {
      CmdResult paused = c.pauseFor(*minutes);
      if (!paused.ok) return {errorJson(paused.error, paused.field, paused.message), true};
    } else {
      c.pause();
    }
  } else if (name == "resume") {
    CmdResult resumed = c.resume();
    if (!resumed.ok) return {errorJson(resumed.error, resumed.field, resumed.message), true};
  } else if (name == "stop") c.stop();
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

bool isBaseTool(const std::string& name) {
  for (const char* known : kToolNames)
    if (name == known) return true;
  return false;
}

McpReply handleToolsCall(ApiContext& ctx, bool extended, JsonVariantConst id, JsonVariantConst params) {
  if (!params.is<JsonObjectConst>()) return replyError(id, kInvalidParams, "params must be an object");
  if (!params["name"].is<const char*>()) return replyError(id, kInvalidParams, "missing tool name");
  const std::string name = params["name"].as<std::string>();
  const bool base = isBaseTool(name);
  if (!base && !(extended && findExtTool(ctx, name))) return replyError(id, kInvalidParams, "unknown tool");
  JsonVariantConst args = params["arguments"];
  if (!args.isNull() && !args.is<JsonObjectConst>())
    return replyError(id, kInvalidParams, "arguments must be an object");

  ToolOutput out = base ? callBaseTool(ctx.controller, name, args) : callExtTool(ctx, name, args);
  JsonDocument doc;
  doc["jsonrpc"] = "2.0";
  doc["id"] = id;
  JsonObject content = doc["result"]["content"].add<JsonObject>();
  content["type"] = "text";
  content["text"] = out.text;
  doc["result"]["isError"] = out.isError;
  return {200, serialize(doc)};
}

McpReply handle(ApiContext& ctx, bool extended, const std::string& body) {
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
    JsonArray tools = reply["result"]["tools"].to<JsonArray>();
    fillBaseTools(tools);
    if (extended) fillExtTools(tools, ctx);
  } else if (method == "tools/call") {
    return handleToolsCall(ctx, extended, id, req["params"]);
  } else {
    return replyError(id, kMethodNotFound, "method not found");
  }
  return {200, serialize(reply)};
}

}  // namespace

McpReply handleMcp(Controller& c, const std::string& body) {
  ApiContext bare{c};
  return handle(bare, false, body);
}

McpReply handleMcpEx(ApiContext& ctx, const std::string& body) { return handle(ctx, true, body); }

}  // namespace jig
