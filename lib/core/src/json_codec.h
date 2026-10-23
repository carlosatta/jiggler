#pragma once

#include <ArduinoJson.h>

#include <optional>
#include <string>

#include "config.h"
#include "controller.h"

namespace jig {

struct ParseError {
  std::string code;  // bad_json | invalid_config
  std::string field;
  std::string message;
};

std::string statusToJson(const Status& status);
std::string configToJson(const Config& config);
std::string errorJson(const std::string& code, const std::string& field = "",
                      const std::string& message = "");

// Type-checks every present field; unknown fields are rejected. Range checks live in applyPatch.
// Optional {"minutes": 1..1440} on start. `minutes` stays empty when there is no limit (empty body or "{}").
bool parseStartRequest(JsonObjectConst obj, std::optional<uint32_t>& minutes, ParseError& err);
bool parseStartRequest(const std::string& body, std::optional<uint32_t>& minutes, ParseError& err);

bool parseConfigPatch(const std::string& body, ConfigPatch& out, ParseError& err);
bool parseConfigPatch(JsonObjectConst obj, ConfigPatch& out, ParseError& err);

}  // namespace jig
