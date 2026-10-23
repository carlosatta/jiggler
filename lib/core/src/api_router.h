#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "controller.h"

namespace jig {

constexpr size_t kMaxBodyBytes = 1024;

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

// True only for "Bearer <token>" with a non-empty configured token (constant-time compare).
bool authorized(const std::string& authorizationHeader, const std::string& token);

// `path` must not include a query string. Authentication is the caller's job.
HttpResponse handleApi(Controller& controller, const std::string& method, const std::string& path,
                       const std::string& body);

}  // namespace jig
