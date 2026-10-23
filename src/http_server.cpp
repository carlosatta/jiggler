#include "http_server.h"

#include <ESPAsyncWebServer.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "api_router.h"
#include "dashboard_html.h"
#include "json_codec.h"
#include "mcp_server.h"

struct HttpServer::Impl {
  Impl(jig::Controller& c, TokenStore& t) : controller(c), tokens(t), server(80) {}

  static void json(AsyncWebServerRequest* req, int status, const std::string& body) {
    AsyncWebServerResponse* res = req->beginResponse(status, "application/json", body.c_str());
    res->addHeader("Connection", "close");
    req->send(res);
  }

  static void onBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
    if (total > jig::kMaxBodyBytes) return;  // rejected with 413 in handle()
    if (index == 0) req->_tempObject = calloc(total + 1, 1);  // freed by the library with the request
    if (req->_tempObject) memcpy(static_cast<uint8_t*>(req->_tempObject) + index, data, len);
  }

  void handle(AsyncWebServerRequest* req) {
    const std::string path = req->url().c_str();
    const bool isMcp = path == "/mcp";
    if (!isMcp && path.rfind("/api/", 0) != 0) return json(req, 404, jig::errorJson("not_found"));

    const AsyncWebHeader* auth = req->getHeader("Authorization");
    if (!jig::authorized(auth ? auth->value().c_str() : "", tokens.load())) {
      AsyncWebServerResponse* res = req->beginResponse(401, "application/json", jig::errorJson("unauthorized").c_str());
      res->addHeader("WWW-Authenticate", "Bearer");
      res->addHeader("Connection", "close");
      return req->send(res);
    }

    const size_t contentLength = req->contentLength();
    if (contentLength > jig::kMaxBodyBytes) return json(req, 413, jig::errorJson("payload_too_large"));
    const std::string body = (req->_tempObject && contentLength > 0)
                                 ? std::string(static_cast<const char*>(req->_tempObject), contentLength)
                                 : std::string();
    const std::string method = req->methodToString();

    if (isMcp) {
      if (method != "POST") return json(req, 405, jig::errorJson("method_not_allowed"));
      jig::McpReply r = jig::handleMcp(controller, body);
      if (r.httpStatus == 202) return req->send(202);
      return json(req, r.httpStatus, r.body);
    }
    jig::HttpResponse r = jig::handleApi(controller, method, path, body);
    json(req, r.status, r.body);
  }

  jig::Controller& controller;
  TokenStore& tokens;
  AsyncWebServer server;
};

HttpServer::HttpServer(jig::Controller& controller, TokenStore& tokens)
    : impl_(new Impl(controller, tokens)) {}
HttpServer::~HttpServer() = default;

void HttpServer::begin() {
  Impl* s = impl_.get();
  s->server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    AsyncWebServerResponse* res = req->beginResponse(200, "text/html", kDashboardHtml);
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });
  s->server.onRequestBody(Impl::onBody);
  s->server.onNotFound([s](AsyncWebServerRequest* req) { s->handle(req); });
  s->server.begin();
}
