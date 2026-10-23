#include "api_router.h"

#include "json_codec.h"

namespace jig {

size_t copyBodyChunk(char* dst, size_t total, const uint8_t* data, size_t len, size_t index) {
  if (index >= total) return 0;
  const size_t n = len < total - index ? len : total - index;
  for (size_t i = 0; i < n; i++) dst[index + i] = static_cast<char>(data[i]);
  return n;
}

std::optional<HttpResponse> rejectBody(size_t contentLength, bool captured) {
  if (contentLength > kMaxBodyBytes) return HttpResponse{413, errorJson("payload_too_large")};
  if (contentLength > 0 && !captured)
    return HttpResponse{415, errorJson("unsupported_media_type", "", "send the body with Content-Type: application/json")};
  return std::nullopt;
}

bool authorized(const std::string& header, const std::string& token) {
  static const std::string kPrefix = "Bearer ";
  if (token.empty() || header.compare(0, kPrefix.size(), kPrefix) != 0) return false;
  const std::string presented = header.substr(kPrefix.size());
  uint8_t diff = presented.size() != token.size();
  for (size_t i = 0; i < token.size(); i++)
    diff |= static_cast<uint8_t>(token[i]) ^ (i < presented.size() ? static_cast<uint8_t>(presented[i]) : 0);
  return diff == 0;
}

namespace {

HttpResponse json(int status, std::string body) { return {status, std::move(body)}; }

HttpResponse putConfig(Controller& c, const std::string& body) {
  if (body.size() > kMaxBodyBytes) return json(413, errorJson("payload_too_large"));
  ConfigPatch patch;
  ParseError perr;
  if (!parseConfigPatch(body, patch, perr)) return json(400, errorJson(perr.code, perr.field, perr.message));
  CmdResult r = c.updateConfig(patch);
  if (!r.ok) return json(400, errorJson(r.error, r.field, r.message));
  return json(200, configToJson(c.status().config));
}

}  // namespace

namespace {
struct Route {
  const char* path;
  const char* methods[2];
};

const Route kRoutes[] = {
    {"/api/status", {"GET", nullptr}}, {"/api/start", {"POST", nullptr}},
    {"/api/stop", {"POST", nullptr}},  {"/api/pause", {"POST", nullptr}},
    {"/api/move", {"POST", nullptr}},  {"/api/config", {"GET", "PUT"}},
};
}  // namespace

HttpResponse handleApi(Controller& c, const std::string& method, const std::string& path,
                       const std::string& body) {
  const Route* route = nullptr;
  for (const Route& r : kRoutes)
    if (path == r.path) route = &r;
  if (!route) return json(404, errorJson("not_found"));

  bool methodOk = false;
  for (const char* m : route->methods)
    if (m && method == m) methodOk = true;
  if (!methodOk) return json(405, errorJson("method_not_allowed"));

  if (path == "/api/status") return json(200, statusToJson(c.status()));
  if (path == "/api/start") c.start();
  else if (path == "/api/stop") c.stop();
  else if (path == "/api/pause") c.pause();
  else if (path == "/api/move") {
    CmdResult r = c.moveNow();
    if (!r.ok) return json(409, errorJson(r.error, r.field, r.message));
    return json(202, "{\"queued\":true}");
  } else if (path == "/api/config") {
    if (method == "GET") return json(200, configToJson(c.status().config));
    return putConfig(c, body);
  }
  return json(200, statusToJson(c.status()));
}

}  // namespace jig
