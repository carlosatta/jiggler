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

HttpResponse devicesRoute(ApiContext& ctx, const std::string& method, const std::string& body) {
  if (!ctx.devices) return json(404, errorJson("not_found"));
  if (method == "GET") return json(200, devicesToJson(ctx.devices->list()));
  if (method != "PUT") return json(405, errorJson("method_not_allowed"));
  if (body.size() > kMaxMediumBodyBytes) return json(413, errorJson("payload_too_large"));

  std::vector<DeviceConfig> devices;
  ParseError err;
  if (!parseDevices(body, devices, err)) return json(400, errorJson(err.code, err.field, err.message));
  CmdResult r = ctx.devices->replace(devices);
  if (!r.ok) return json(statusForCmdError(r.error), errorJson(r.error, r.field, r.message));
  return json(200, devicesToJson(ctx.devices->list(), true));
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
  if (path == "/api/network") return networkRoute(ctx, method, body);
  if (path == "/api/devices") return devicesRoute(ctx, method, body);
  if (path == "/api/automations") return automationsRoute(ctx, method, body);
  if (path == "/api/scripts" || path.rfind("/api/scripts/", 0) == 0) return scriptsRoute(ctx, method, path, body);
  if (path == "/api/profiles") {
    if (!ctx.devices) return json(404, errorJson("not_found"));
    if (method != "GET") return json(405, errorJson("method_not_allowed"));
    return json(200, profilesToJson());
  }
  if (path == "/api/restart") {
    if (!ctx.restart) return json(404, errorJson("not_found"));
    if (method != "POST") return json(405, errorJson("method_not_allowed"));
    ctx.restart();
    return json(202, "{\"restarting\":true}");
  }
  return handleApi(ctx.controller, method, path, body);
}

}  // namespace jig
