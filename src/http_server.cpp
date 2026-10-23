#include "http_server.h"

#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include <cstdlib>
#include <string>

#include "api_router.h"
#include "dashboard_html.h"
#include "json_codec.h"
#include "mcp_server.h"

struct HttpServer::Impl {
  Impl(jig::ApiContext& a, TokenStore& t) : api(a), tokens(t), server(80) {}

  static void json(AsyncWebServerRequest* req, int status, const std::string& body) {
    AsyncWebServerResponse* res = req->beginResponse(status, "application/json", body.c_str());
    res->addHeader("Connection", "close");
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  }

  static void onBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
    if (total > jig::kMaxBodyBytes || index >= total) return;  // refused in handle()
    if (index == 0 && !req->_tempObject) req->_tempObject = calloc(total + 1, 1);  // freed by the library
    if (!req->_tempObject) return;
    jig::copyBodyChunk(static_cast<char*>(req->_tempObject), total, data, len, index);
  }

  void handle(AsyncWebServerRequest* req) {
    const std::string path = req->url().c_str();
    const bool isMcp = path == "/mcp";
    if (!isMcp && path.rfind("/api/", 0) != 0) {
      if (captive) return req->redirect(String("http://") + WiFi.softAPIP().toString() + "/");
      return json(req, 404, jig::errorJson("not_found"));
    }

    const AsyncWebHeader* auth = req->getHeader("Authorization");
    if (!jig::authorized(auth ? auth->value().c_str() : "", tokens.load())) {
      AsyncWebServerResponse* res = req->beginResponse(401, "application/json", jig::errorJson("unauthorized").c_str());
      res->addHeader("WWW-Authenticate", "Bearer");
      res->addHeader("Connection", "close");
      return req->send(res);
    }

    const size_t contentLength = req->contentLength();
    if (auto rejected = jig::rejectBody(contentLength, req->_tempObject != nullptr))
      return json(req, rejected->status, rejected->body);
    const std::string body = (req->_tempObject && contentLength > 0)
                                 ? std::string(static_cast<const char*>(req->_tempObject), contentLength)
                                 : std::string();
    const std::string method = req->methodToString();

    if (isMcp) {
      if (method != "POST") return json(req, 405, jig::errorJson("method_not_allowed"));
      jig::McpReply r = jig::handleMcp(api.controller, body);
      if (r.httpStatus == 202) return req->send(202);
      return json(req, r.httpStatus, r.body);
    }
    jig::HttpResponse r = jig::handleApiEx(api, method, path, body);
    json(req, r.status, r.body);
  }

  jig::ApiContext& api;
  TokenStore& tokens;
  AsyncWebServer server;
  bool captive = false;
};

HttpServer::HttpServer(jig::ApiContext& api, TokenStore& tokens) : impl_(new Impl(api, tokens)) {}
HttpServer::~HttpServer() = default;

void HttpServer::begin(bool captive) {
  Impl* s = impl_.get();
  s->captive = captive;
  s->server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    AsyncWebServerResponse* res = req->beginResponse(200, "text/html", kDashboardHtml);
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });
  s->server.onRequestBody(Impl::onBody);
  s->server.onNotFound([s](AsyncWebServerRequest* req) { s->handle(req); });
  s->server.begin();
}
