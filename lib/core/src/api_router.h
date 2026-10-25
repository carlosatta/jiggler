#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "controller.h"

namespace jig {

// Transport cap (HTTP server, MCP, script upload); small JSON routes enforce tighter limits themselves.
constexpr size_t kMaxBodyBytes = 4096;
constexpr size_t kMaxSmallBodyBytes = 1024;   // /api/config, /api/network
constexpr size_t kMaxMediumBodyBytes = 4096;  // /api/schedules, /api/automations

struct HttpResponse {
  int status;
  std::string body;  // always application/json
};

// Copies a body chunk into a buffer of `total` bytes without writing past it: the HTTP library may
// hand over more bytes than the declared Content-Length. Returns the number of bytes copied.
size_t copyBodyChunk(char* dst, size_t total, const uint8_t* data, size_t len, size_t index);

// Response for a body that must be refused, else nullopt: 413 over kMaxBodyBytes, 415 when a body
// was declared but never captured (form-encoded bodies are parsed as params by the HTTP library).
std::optional<HttpResponse> rejectBody(size_t contentLength, bool captured);

// Compares in time that depends only on the length of `expected`, not on where the strings differ.
bool constantTimeEquals(const std::string& presented, const std::string& expected);

// True only for "Bearer <token>" with a non-empty configured token (constant-time compare).
bool authorized(const std::string& authorizationHeader, const std::string& token);

// `path` must not include a query string. Authentication is the caller's job.
HttpResponse handleApi(Controller& controller, const std::string& method, const std::string& path,
                       const std::string& body);

}  // namespace jig
