#pragma once

#include <memory>

#include "controller.h"
#include "token_store.h"

// REST API (/api/*) and MCP (/mcp) over HTTP on port 80, bearer-token protected.
class HttpServer {
 public:
  HttpServer(jig::Controller& controller, TokenStore& tokens);
  ~HttpServer();

  void begin();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
