#pragma once

#include <cstddef>
#include <string>

#include "controller.h"

namespace jig {

constexpr size_t kMaxBodyBytes = 1024;

struct HttpResponse {
  int status;
  std::string body;  // always application/json
};

// True only for "Bearer <token>" with a non-empty configured token (constant-time compare).
bool authorized(const std::string& authorizationHeader, const std::string& token);

// `path` must not include a query string. Authentication is the caller's job.
HttpResponse handleApi(Controller& controller, const std::string& method, const std::string& path,
                       const std::string& body);

}  // namespace jig
