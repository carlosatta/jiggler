#include "http_server.h"

#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <WiFi.h>

#include <cstdlib>
#include <string>

#include "api_router.h"
#include "dashboard_html.h"
#include "json_codec.h"
#include "mcp_server.h"
#include "web_assets.h"

namespace {
// State of one firmware upload, kept in the request (freed by the library with the request).
struct UpdateState {
  bool authorized;
  bool started;
  bool done;
  uint32_t received;
  uint32_t lastChunkMs;
  char error[96];
};

void setError(UpdateState* st, const char* message) { snprintf(st->error, sizeof(st->error), "%s", message); }

// What a browser gets while firmware downloads. Kept in flash and sent as is: serving the real dashboard means
// 60 KB through the async server's buffers, and the secure download needs that memory more than the page does.
const char kInstallingPage[] =
    "<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<meta http-equiv=refresh content=15><title>Installing</title>"
    "<body style=\"font:16px system-ui;margin:3rem;max-width:30rem\"><h1>Installing new firmware</h1>"
    "<p>The device is downloading and writing the new version, and answers again once it has restarted. "
    "This page comes back by itself.</p>";

bool installing(jig::ApiContext& api) { return api.updates && api.updates->installActivity().active; }

void sendInstalling(AsyncWebServerRequest* req) {
  AsyncWebServerResponse* res = req->beginResponse(503, "text/html", reinterpret_cast<const uint8_t*>(kInstallingPage), sizeof(kInstallingPage) - 1);
  res->addHeader("Cache-Control", "no-store");
  res->addHeader("Retry-After", "15");
  req->send(res);
}

}  // namespace

struct HttpServer::Impl {
  Impl(jig::ApiContext& a, TokenStore& t) : api(a), tokens(t), server(80) {}

  static void json(AsyncWebServerRequest* req, int status, const std::string& body) {
    AsyncWebServerResponse* res = req->beginResponse(status, "application/json", body.c_str());
    res->addHeader("Connection", "close");
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  }

  bool isAuthorized(AsyncWebServerRequest* req) {
    const AsyncWebHeader* auth = req->getHeader("Authorization");
    return jig::authorized(auth ? auth->value().c_str() : "", tokens.load());
  }

  void acceptBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
    if (total > jig::kMaxBodyBytes || index >= total) return;  // refused in handle()
    // No buffer for unauthenticated requests: handle() answers them with 401 without a body.
    if (index == 0 && !req->_tempObject && isAuthorized(req)) req->_tempObject = calloc(total + 1, 1);  // freed by the library
    if (!req->_tempObject) return;
    jig::copyBodyChunk(static_cast<char*>(req->_tempObject), total, data, len, index);
  }

  static void onBody(Impl* self, AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
    self->acceptBody(req, data, len, index, total);
  }

  // POST /api/update, multipart form with the firmware .bin. Nothing is written unless the token is right.
  void handleUpload(AsyncWebServerRequest* req, size_t index, uint8_t* data, size_t len, bool final) {
    UpdateState* st = static_cast<UpdateState*>(req->_tempObject);
    if (index == 0) {
      if (!st) {
        st = static_cast<UpdateState*>(calloc(1, sizeof(UpdateState)));
        req->_tempObject = st;
      }
      if (!st) return;
      st->authorized = isAuthorized(req);
      Serial.printf("OTA: upload starts, authorized=%d, free heap %u\n", st->authorized ? 1 : 0, static_cast<unsigned>(ESP.getFreeHeap()));
      if (!st->authorized) return;
      if (Update.isRunning()) {
        Serial.println("OTA: dropping an unfinished earlier update");
        Update.abort();
      }
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        setError(st, Update.errorString());
        Serial.printf("OTA: begin failed: %s\n", st->error);
        return;
      }
      st->started = true;
      if (api.updateActive) api.updateActive(true);
      st->lastChunkMs = millis();
      req->client()->onError([](void*, AsyncClient* c, int8_t err) { Serial.printf("OTA: TCP error %d (%s)\n", err, c->errorToString(err)); }, nullptr);
      req->client()->onTimeout([](void*, AsyncClient*, uint32_t t) { Serial.printf("OTA: TCP timeout %u ms\n", static_cast<unsigned>(t)); }, nullptr);
      req->onDisconnect([this, st]() {
        Serial.printf("OTA: connection closed after %u bytes (done=%d)\n", static_cast<unsigned>(st->received), st->done ? 1 : 0);
        if (st->started && !st->done) {  // cut off mid-way: do not leave the updater half-open
          Update.abort();
          st->started = false;
          if (api.updateActive) api.updateActive(false);
        }
      });
    }
    if (!st || !st->started) return;
    const uint32_t now = millis();
    if (now - st->lastChunkMs > 1500) Serial.printf("OTA: %u ms without data before byte %u\n", static_cast<unsigned>(now - st->lastChunkMs), static_cast<unsigned>(index));
    st->lastChunkMs = now;
    st->received = static_cast<uint32_t>(index + len);
    if ((index / 131072) != ((index + len) / 131072)) Serial.printf("OTA: %u KB at %lu ms, heap %u\n", static_cast<unsigned>((index + len) / 1024), static_cast<unsigned long>(now), static_cast<unsigned>(ESP.getFreeHeap()));
    if (len > 0 && Update.write(data, len) != len) {
      setError(st, Update.errorString());
      Serial.printf("OTA: write failed at %u: %s\n", static_cast<unsigned>(index), st->error);
      Update.abort();
      st->started = false;
      if (api.updateActive) api.updateActive(false);
      return;
    }
    if (final) {
      st->started = false;
      if (Update.end(true)) st->done = true;
      else setError(st, Update.errorString());
      Serial.printf("OTA: upload finished, %s\n", st->done ? "ok" : st->error);
      if (!st->done && api.updateActive) api.updateActive(false);
    }
  }

  void finishUpdate(AsyncWebServerRequest* req) {
    UpdateState* st = static_cast<UpdateState*>(req->_tempObject);
    Serial.printf("OTA: request complete, state %s\n", !st ? "none" : st->done ? "done" : st->error[0] ? st->error : "incomplete");
    if (!st ? !isAuthorized(req) : !st->authorized) {
      AsyncWebServerResponse* res = req->beginResponse(401, "application/json", jig::errorJson("unauthorized").c_str());
      res->addHeader("Connection", "close");
      return req->send(res);
    }
    if (!st) return json(req, 400, jig::errorJson("update_failed", "", "no firmware file was sent"));
    if (st->error[0]) return json(req, 400, jig::errorJson("update_failed", "", st->error));
    if (!st->done) return json(req, 400, jig::errorJson("update_failed", "", "the upload was incomplete"));
    json(req, 200, "{\"updated\":true,\"restarting\":true}");
    if (api.restart) api.restart();  // lets this response go out first
  }

  void handle(AsyncWebServerRequest* req) {
    const std::string path = req->url().c_str();
    const bool isMcp = path == "/mcp";
    if (!isMcp && path.rfind("/api/", 0) != 0) {
      if (captive) return req->redirect(String("http://") + WiFi.softAPIP().toString() + "/");
      return json(req, 404, jig::errorJson("not_found"));
    }

    if (!isAuthorized(req)) {
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
      jig::McpReply r = jig::handleMcpEx(api, body);
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

  void servePng(const char* path, const uint8_t* data, size_t size) {
    server.on(path, HTTP_GET, [this, data, size](AsyncWebServerRequest* req) {
      if (api.updates && api.updates->installActivity().active) return sendInstalling(req);
      AsyncWebServerResponse* res = req->beginResponse(200, "image/png", data, size);
      res->addHeader("Cache-Control", "public, max-age=86400");
      req->send(res);
    });
  }
};

HttpServer::HttpServer(jig::ApiContext& api, TokenStore& tokens) : impl_(new Impl(api, tokens)) {}
HttpServer::~HttpServer() = default;

void HttpServer::begin(bool captive) {
  Impl* s = impl_.get();
  s->captive = captive;
  s->server.on("/", HTTP_GET, [s](AsyncWebServerRequest* req) {
    if (installing(s->api)) return sendInstalling(req);
    AsyncWebServerResponse* res = req->beginResponse(200, "text/html", reinterpret_cast<const uint8_t*>(kDashboardHtml), sizeof(kDashboardHtml) - 1);  // streamed from flash, never copied to the heap
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });
  s->server.on("/manifest.webmanifest", HTTP_GET, [s](AsyncWebServerRequest* req) {
    if (installing(s->api)) return sendInstalling(req);
    AsyncWebServerResponse* res = req->beginResponse(200, "application/manifest+json", reinterpret_cast<const uint8_t*>(webassets::kManifest), sizeof(webassets::kManifest) - 1);
    res->addHeader("Cache-Control", "public, max-age=86400");
    req->send(res);
  });
  s->servePng("/icon-192.png", webassets::kIcon192, webassets::kIcon192Size);
  s->servePng("/icon-512.png", webassets::kIcon512, webassets::kIcon512Size);
  s->servePng("/apple-touch-icon.png", webassets::kAppleTouchIcon, webassets::kAppleTouchIconSize);
  s->server.on(
      "/api/update", HTTP_POST, [s](AsyncWebServerRequest* req) { s->finishUpdate(req); },
      [s](AsyncWebServerRequest* req, const String&, size_t index, uint8_t* data, size_t len, bool final) {
        s->handleUpload(req, index, data, len, final);
      });
  s->server.onRequestBody([s](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
    Impl::onBody(s, req, data, len, index, total);
  });
  s->server.onNotFound([s](AsyncWebServerRequest* req) { s->handle(req); });
  s->server.begin();
}
