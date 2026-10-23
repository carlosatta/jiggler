#pragma once

#include <string>

#include "api_ex.h"
#include "controller.h"

namespace jig {

struct McpReply {
  int httpStatus;
  std::string body;  // application/json; empty for notifications (202)
};

// Stateless MCP over HTTP: one JSON-RPC 2.0 message in, one JSON message out.
// handleMcp exposes the original six jiggler tools only.
McpReply handleMcp(Controller& controller, const std::string& body);

// handleMcpEx adds the tools for devices, scripts, automations, keyboard and network; a tool is listed
// (and callable) only when its service is present in the context. The original six come first.
McpReply handleMcpEx(ApiContext& ctx, const std::string& body);

}  // namespace jig
