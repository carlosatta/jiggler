#pragma once

#include <string>

#include "controller.h"

namespace jig {

struct McpReply {
  int httpStatus;
  std::string body;  // application/json; empty for notifications (202)
};

// Stateless MCP over HTTP: one JSON-RPC 2.0 message in, one JSON message out.
McpReply handleMcp(Controller& controller, const std::string& body);

}  // namespace jig
