#include "api_ex.h"

namespace jig {

namespace {

HttpResponse json(int status, std::string body) { return {status, std::move(body)}; }

HttpResponse networkRoute(ApiContext& ctx, const std::string& method, const std::string& body) {
  if (!ctx.network) return json(404, errorJson("not_found"));
  if (method == "GET") return json(200, networkToJson(ctx.network->load()));
  if (method != "PUT") return json(405, errorJson("method_not_allowed"));
  if (body.size() > kMaxBodyBytes) return json(413, errorJson("payload_too_large"));

  NetworkSettings settings = ctx.network->load();
  ParseError err;
  if (!parseNetworkPatch(body, settings, err)) return json(400, errorJson(err.code, err.field, err.message));
  ctx.network->save(settings);
  return json(200, networkToJson(settings));
}

}  // namespace

HttpResponse handleApiEx(ApiContext& ctx, const std::string& method, const std::string& path,
                         const std::string& body) {
  if (path == "/api/network") return networkRoute(ctx, method, body);
  if (path == "/api/restart") {
    if (!ctx.restart) return json(404, errorJson("not_found"));
    if (method != "POST") return json(405, errorJson("method_not_allowed"));
    ctx.restart();
    return json(202, "{\"restarting\":true}");
  }
  return handleApi(ctx.controller, method, path, body);
}

}  // namespace jig
