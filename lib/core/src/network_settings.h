#pragma once

#include <optional>
#include <string>

#include "config.h"
#include "json_codec.h"

namespace jig {

enum class NetworkMode { Auto, Ap };

struct NetworkSettings {
  NetworkMode mode = NetworkMode::Auto;
  std::string apSsid = "Jiggler";
  std::string apPassword;  // empty = generated on first AP start
};

struct NetworkStore {
  virtual ~NetworkStore() = default;
  virtual NetworkSettings load() = 0;
  virtual void save(const NetworkSettings& settings) = 0;
};

std::optional<ValidationError> validateNetwork(const NetworkSettings& settings);
std::string networkToJson(const NetworkSettings& settings);

// Partial update: `base` changes only on success.
bool parseNetworkPatch(const std::string& body, NetworkSettings& base, ParseError& err);

// Probe URLs phones/OSes request to detect a captive portal.
bool isCaptiveProbePath(const std::string& path);

}  // namespace jig
