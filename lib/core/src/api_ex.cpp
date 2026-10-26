#include "api_ex.h"

namespace jig {

namespace {

HttpResponse json(int status, std::string body) { return {status, std::move(body)}; }

int statusForCmdError(const std::string& error) {
  if (error == "not_found") return 404;
  if (error == "read_only") return 403;
  if (error == "limit_reached" || error == "mouse_not_connected" || error == "busy") return 409;
  if (error == "storage_error") return 507;
  return 400;
}

HttpResponse bluetoothRoute(ApiContext& ctx, const std::string& method, const std::string& path, const std::string& body) {
  BluetoothManager* bt = ctx.bluetooth;
  if (!bt) return json(404, errorJson("not_found"));
  auto reply = [&](const CmdResult& r) {
    if (r.ok) return json(200, bluetoothToJson(bt->status()));
    return json(statusForCmdError(r.error), errorJson(r.error, r.field, r.message));
  };
  static const std::string kBase = "/api/bluetooth";
  const std::string rest = path.substr(kBase.size());  // "", "/pairing", "/peers/<id>", ...

  if (rest.empty()) {
    if (method != "GET") return json(405, errorJson("method_not_allowed"));
    return json(200, bluetoothToJson(bt->status()));
  }
  if (rest == "/pairing") {
    if (method == "DELETE") return reply(bt->stopPairing());
    if (method != "POST") return json(405, errorJson("method_not_allowed"));
    if (body.size() > kMaxSmallBodyBytes) return json(413, errorJson("payload_too_large"));
    int64_t seconds = 0;  // 0 = the configured Discovery length
    if (!body.empty()) {
      JsonDocument doc;
      if (deserializeJson(doc, body) || !doc.is<JsonObject>()) return json(400, errorJson("bad_json", "", "body must be a JSON object"));
      if (!doc["seconds"].isNull()) {
        if (!doc["seconds"].is<int64_t>()) return json(400, errorJson("invalid_seconds", "seconds", "must be an integer"));
        seconds = doc["seconds"].as<int64_t>();
      }
    }
    if (seconds < 0 || seconds > 1000000) seconds = 0;  // out of range: let the manager reject it
    return reply(bt->startPairing(static_cast<uint32_t>(seconds)));
  }
  if (rest == "/enable" || rest == "/disable") {
    if (method != "POST") return json(405, errorJson("method_not_allowed"));
    return reply(rest == "/enable" ? bt->enable() : bt->disable());
  }
  if (rest == "/settings") {
    if (method != "PUT") return json(405, errorJson("method_not_allowed"));
    if (body.size() > kMaxSmallBodyBytes) return json(413, errorJson("payload_too_large"));
    JsonDocument doc;
    if (deserializeJson(doc, body) || !doc.is<JsonObject>()) return json(400, errorJson("bad_json", "", "body must be a JSON object"));
    std::optional<uint32_t> seconds;
    std::optional<bool> offWhen;
    for (JsonPair kv : doc.as<JsonObject>()) {
      const std::string key = kv.key().c_str();
      if (key == "discoverySeconds") {
        if (!kv.value().is<uint32_t>()) return json(400, errorJson("invalid_seconds", key, "must be a whole number of seconds"));
        seconds = kv.value().as<uint32_t>();
      } else if (key == "offWhenComputerDisconnects") {
        if (!kv.value().is<bool>()) return json(400, errorJson("invalid_setting", key, "must be true or false"));
        offWhen = kv.value().as<bool>();
      } else if (key != "enabled" && key != "state") {  // read-only keys a GET body carries
        return json(400, errorJson("unknown_field", key, "unknown field"));
      }
    }
    return reply(bt->updateSettings(seconds, offWhen));
  }
  if (rest == "/peers") {
    if (method != "DELETE") return json(405, errorJson("method_not_allowed"));
    return reply(bt->forgetAll());
  }
  if (rest.rfind("/peers/", 0) == 0) {
    const std::string id = rest.substr(7);
    if (id.empty() || id.find('/') != std::string::npos) return json(404, errorJson("not_found"));
    if (method == "DELETE") return reply(bt->forget(id));
    if (method != "PUT") return json(405, errorJson("method_not_allowed"));
    if (body.size() > kMaxSmallBodyBytes) return json(413, errorJson("payload_too_large"));
    JsonDocument doc;
    if (deserializeJson(doc, body) || !doc.is<JsonObject>()) return json(400, errorJson("bad_json", "", "body must be a JSON object"));
    if (!doc["label"].is<const char*>()) return json(400, errorJson("invalid_label", "label", "must be a string"));
    return reply(bt->rename(id, doc["label"].as<std::string>()));
  }
  return json(404, errorJson("not_found"));
}

bool validToken(const std::string& t) {
  if (t.size() < 8 || t.size() > 40) return false;
  for (unsigned char c : t)
    if (c < 0x21 || c > 0x7E) return false;  // printable ASCII, no spaces
  return true;
}

HttpResponse tokenRoute(ApiContext& ctx, const std::string& method, const std::string& body) {
  if (!ctx.tokens) return json(404, errorJson("not_found"));
  if (method != "PUT") return json(405, errorJson("method_not_allowed"));
  if (body.size() > kMaxSmallBodyBytes) return json(413, errorJson("payload_too_large"));
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc.is<JsonObject>()) return json(400, errorJson("bad_json", "", "body must be a JSON object"));
  if (!doc["current"].is<const char*>()) return json(400, errorJson("invalid_token", "current", "the current token is required"));
  if (!doc["token"].is<const char*>() || !validToken(doc["token"].as<std::string>()))
    return json(400, errorJson("invalid_token", "token", "must be 8 to 40 printable characters without spaces"));
  if (!constantTimeEquals(doc["current"].as<std::string>(), ctx.tokens->load()))
    return json(403, errorJson("wrong_current", "current", "the current token is not correct"));
  if (!ctx.tokens->save(doc["token"].as<std::string>()))
    return json(507, errorJson("storage_error", "", "could not save the token (storage full?)"));
  return json(200, "{\"changed\":true}");
}

HttpResponse statusWithExtras(ApiContext& ctx, HttpResponse base) {
  if (base.status != 200 || base.body.empty() || base.body.back() != '}') return base;
  std::string extra;
  if (ctx.bluetooth) extra += ",\"bluetooth\":" + bluetoothSummaryJson(ctx.bluetooth->status());
  if (ctx.clock) extra += ",\"time\":" + clockStatusToJson(ctx.clock->status());
  if (ctx.schedules) extra += ",\"schedule\":" + scheduleStatusToJson(ctx.schedules->status());
  if (!ctx.info.version.empty()) {
    JsonDocument doc;
    doc["version"] = ctx.info.version;
    doc["bleName"] = ctx.info.bleName;
    doc["bleAddress"] = ctx.info.bleAddress;
    std::string device;
    serializeJson(doc, device);
    extra += ",\"device\":" + device;
  }
  if (extra.empty()) return base;
  base.body.pop_back();
  base.body += extra + "}";
  return base;
}

HttpResponse networkRoute(ApiContext& ctx, const std::string& method, const std::string& body) {
  if (!ctx.network) return json(404, errorJson("not_found"));
  if (method == "GET") return json(200, networkToJson(ctx.network->load()));
  if (method != "PUT") return json(405, errorJson("method_not_allowed"));
  if (body.size() > kMaxSmallBodyBytes) return json(413, errorJson("payload_too_large"));

  NetworkSettings settings = ctx.network->load();
  ParseError err;
  if (!parseNetworkPatch(body, settings, err)) return json(400, errorJson(err.code, err.field, err.message));
  ctx.network->save(settings);
  return json(200, networkToJson(settings));
}

HttpResponse appearanceRoute(ApiContext& ctx, const std::string& method, const std::string& body) {
  if (!ctx.appearance) return json(404, errorJson("not_found"));
  if (method == "GET") return json(200, appearanceToJson(ctx.appearance->get()));
  if (method != "PUT") return json(405, errorJson("method_not_allowed"));
  if (body.size() > kMaxSmallBodyBytes) return json(413, errorJson("payload_too_large"));

  Appearance next;
  ParseError err;
  if (!parseAppearance(body, next, err)) return json(400, errorJson(err.code, err.field, err.message));
  CmdResult r = ctx.appearance->set(next);
  if (!r.ok) return json(statusForCmdError(r.error), errorJson(r.error, r.field, r.message));
  return json(200, appearanceToJson(ctx.appearance->get(), true));
}

HttpResponse automationsRoute(ApiContext& ctx, const std::string& method, const std::string& body) {
  if (!ctx.automations) return json(404, errorJson("not_found"));
  if (method == "GET") return json(200, automationsToJson(ctx.automations->list()));
  if (method != "PUT") return json(405, errorJson("method_not_allowed"));
  if (body.size() > kMaxMediumBodyBytes) return json(413, errorJson("payload_too_large"));

  std::vector<Automation> automations;
  ParseError err;
  if (!parseAutomations(body, automations, err)) return json(400, errorJson(err.code, err.field, err.message));
  CmdResult r = ctx.automations->replace(automations);
  if (!r.ok) return json(statusForCmdError(r.error), errorJson(r.error, r.field, r.message));
  return json(200, automationsToJson(ctx.automations->list()));
}

HttpResponse schedulesRoute(ApiContext& ctx, const std::string& method, const std::string& body) {
  if (!ctx.schedules) return json(404, errorJson("not_found"));
  if (method == "GET") return json(200, schedulesToJson(ctx.schedules->settings()));
  if (method != "PUT") return json(405, errorJson("method_not_allowed"));
  if (body.size() > kMaxMediumBodyBytes) return json(413, errorJson("payload_too_large"));

  ScheduleSettings settings = ctx.schedules->settings();
  ParseError err;
  if (!parseSchedules(body, settings, err)) return json(400, errorJson(err.code, err.field, err.message));
  CmdResult r = ctx.schedules->replace(settings);
  if (!r.ok) return json(statusForCmdError(r.error), errorJson(r.error, r.field, r.message));
  return json(200, schedulesToJson(ctx.schedules->settings()));
}

HttpResponse timeRoute(ApiContext& ctx, const std::string& method, const std::string& path, const std::string& body) {
  if (!ctx.clock) return json(404, errorJson("not_found"));
  if (path == "/api/time/set") {
    if (method != "POST") return json(405, errorJson("method_not_allowed"));
    if (body.size() > kMaxSmallBodyBytes) return json(413, errorJson("payload_too_large"));
    JsonDocument doc;
    if (deserializeJson(doc, body) || !doc.is<JsonObject>()) return json(400, errorJson("bad_json", "", "body must be a JSON object"));
    if (!doc["epoch"].is<int64_t>() || doc["epoch"].as<int64_t>() < 0 || doc["epoch"].as<int64_t>() > 0xFFFFFFFFLL)
      return json(400, errorJson("invalid_time", "epoch", "must be a Unix time in seconds"));
    CmdResult r = ctx.clock->setTime(static_cast<uint32_t>(doc["epoch"].as<int64_t>()));
    if (!r.ok) return json(statusForCmdError(r.error), errorJson(r.error, r.field, r.message));
    return json(200, clockToJson(ctx.clock->settings(), ctx.clock->status()));
  }
  if (path != "/api/time") return json(404, errorJson("not_found"));
  if (method == "GET") return json(200, clockToJson(ctx.clock->settings(), ctx.clock->status()));
  if (method != "PUT") return json(405, errorJson("method_not_allowed"));
  if (body.size() > kMaxSmallBodyBytes) return json(413, errorJson("payload_too_large"));
  ClockSettings settings = ctx.clock->settings();
  ParseError err;
  if (!parseClockSettings(body, settings, err)) return json(400, errorJson(err.code, err.field, err.message));
  CmdResult r = ctx.clock->replace(settings);
  if (!r.ok) return json(statusForCmdError(r.error), errorJson(r.error, r.field, r.message));
  return json(200, clockToJson(ctx.clock->settings(), ctx.clock->status()));
}

HttpResponse buttonRoute(ApiContext& ctx, const std::string& method, const std::string& body) {
  if (!ctx.button) return json(404, errorJson("not_found"));
  if (method == "GET") return json(200, buttonToJson(ctx.button->gpio()));
  if (method != "PUT") return json(405, errorJson("method_not_allowed"));
  if (body.size() > kMaxSmallBodyBytes) return json(413, errorJson("payload_too_large"));
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc.is<JsonObject>()) return json(400, errorJson("bad_json", "", "body must be a JSON object"));
  bool hasGpio = false;
  int64_t gpio = 0;
  for (JsonPair kv : doc.as<JsonObject>()) {
    const std::string key = kv.key().c_str();
    if (key == "gpio") {
      if (!kv.value().is<int64_t>()) return json(400, errorJson("invalid_gpio", key, "must be a whole number"));
      gpio = kv.value().as<int64_t>();
      hasGpio = true;
    } else if (key != "default" && key != "gpios") {  // read-only keys a GET body carries
      return json(400, errorJson("unknown_field", key, "unknown field"));
    }
  }
  if (!hasGpio) return json(400, errorJson("invalid_gpio", "gpio", "is required"));
  CmdResult r = ctx.button->setGpio(static_cast<int>(gpio < -1 ? -1 : gpio > 1000 ? 1000 : gpio));
  if (!r.ok) return json(statusForCmdError(r.error), errorJson(r.error, r.field, r.message));
  return json(200, buttonToJson(ctx.button->gpio()));
}

HttpResponse scriptsRoute(ApiContext& ctx, const std::string& method, const std::string& path, const std::string& body) {
  static const std::string kPrefix = "/api/scripts";
  static const std::string kRunSuffix = "/run";
  if (path.size() > kPrefix.size() + 1 + kRunSuffix.size() &&
      path.compare(path.size() - kRunSuffix.size(), kRunSuffix.size(), kRunSuffix) == 0) {
    const std::string name = path.substr(kPrefix.size() + 1, path.size() - kPrefix.size() - 1 - kRunSuffix.size());
    if (name.find('/') != std::string::npos || !ctx.automations) return json(404, errorJson("not_found"));
    if (method != "POST") return json(405, errorJson("method_not_allowed"));
    CmdResult r = ctx.automations->runNow(name);
    if (!r.ok) return json(statusForCmdError(r.error), errorJson(r.error, r.field, r.message));
    return json(202, "{\"queued\":true}");
  }
  if (!ctx.scripts) return json(404, errorJson("not_found"));
  if (path == kPrefix) {
    if (method != "GET") return json(405, errorJson("method_not_allowed"));
    JsonDocument doc;
    JsonArray arr = doc["scripts"].to<JsonArray>();
    for (const std::string& name : ctx.scripts->names()) {
      JsonObject o = arr.add<JsonObject>();
      o["name"] = name;
      o["builtin"] = ctx.scripts->isBuiltin(name);
    }
    std::string out;
    serializeJson(doc, out);
    return json(200, out);
  }

  const std::string name = path.substr(kPrefix.size() + 1);
  if (name.find('/') != std::string::npos) return json(404, errorJson("not_found"));

  if (method == "GET") {
    auto script = ctx.scripts->get(name);
    if (!script) return json(404, errorJson("not_found"));
    JsonDocument doc, parsed;
    deserializeJson(parsed, *script);
    doc["name"] = name;
    doc["builtin"] = ctx.scripts->isBuiltin(name);
    doc["script"] = parsed;
    std::string out;
    serializeJson(doc, out);
    return json(200, out);
  }
  if (method == "PUT") {
    if (body.size() > kMaxBodyBytes) return json(413, errorJson("payload_too_large"));
    CmdResult r = ctx.scripts->put(name, body);
    if (!r.ok) return json(statusForCmdError(r.error), errorJson(r.error, r.field, r.message));
    return json(200, "{\"name\":\"" + name + "\"}");
  }
  if (method == "DELETE") {
    CmdResult r = ctx.scripts->remove(name);
    if (!r.ok) return json(statusForCmdError(r.error), errorJson(r.error, r.field, r.message));
    return json(200, "{\"deleted\":\"" + name + "\"}");
  }
  return json(405, errorJson("method_not_allowed"));
}

}  // namespace

HttpResponse handleApiEx(ApiContext& ctx, const std::string& method, const std::string& path,
                         const std::string& body) {
  if (path == "/api/status" && method == "GET") return statusWithExtras(ctx, handleApi(ctx.controller, method, path, body));
  if (path == "/api/bluetooth" || path.rfind("/api/bluetooth/", 0) == 0) return bluetoothRoute(ctx, method, path, body);
  if (path == "/api/token") return tokenRoute(ctx, method, body);
  if (path == "/api/network") return networkRoute(ctx, method, body);
  if (path == "/api/appearance") return appearanceRoute(ctx, method, body);
  if (path == "/api/automations") return automationsRoute(ctx, method, body);
  if (path == "/api/schedules") return schedulesRoute(ctx, method, body);
  if (path == "/api/time" || path == "/api/time/set") return timeRoute(ctx, method, path, body);
  if (path == "/api/button") return buttonRoute(ctx, method, body);
  if (path == "/api/log") {
    if (!ctx.log) return json(404, errorJson("not_found"));
    if (method != "GET") return json(405, errorJson("method_not_allowed"));
    return json(200, ctx.log->toJson(ctx.nowMs ? ctx.nowMs() : 0));
  }
  if (path == "/api/scripts" || path.rfind("/api/scripts/", 0) == 0) return scriptsRoute(ctx, method, path, body);
  if (path == "/api/restart") {
    if (!ctx.restart) return json(404, errorJson("not_found"));
    if (method != "POST") return json(405, errorJson("method_not_allowed"));
    ctx.restart();
    return json(202, "{\"restarting\":true}");
  }
  return handleApi(ctx.controller, method, path, body);
}

}  // namespace jig
