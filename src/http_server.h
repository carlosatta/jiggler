#pragma once

#include <memory>

#include "api_ex.h"
#include "token_store.h"

// REST API (/api/*) and MCP (/mcp) over HTTP on port 80, bearer-token protected.
class HttpServer {
 public:
  HttpServer(jig::ApiContext& api, TokenStore& tokens);
  ~HttpServer();

  // `captive`: answer unknown pages (OS captive-portal probes included) with a redirect to the dashboard.
  void begin(bool captive);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
