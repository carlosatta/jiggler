#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace jig {

struct Config {
  uint32_t minIntervalSec = 30;
  uint32_t maxIntervalSec = 120;
  uint16_t amplitudePx = 150;
  float clickProbability = 0.0f;
  float scrollProbability = 0.10f;
};

struct ConfigPatch {
  std::optional<int64_t> minIntervalSec;
  std::optional<int64_t> maxIntervalSec;
  std::optional<int64_t> amplitudePx;
  std::optional<double> clickProbability;
  std::optional<double> scrollProbability;
};

struct ValidationError {
  std::string field;
  std::string message;
};

// nullopt on success; `out` is written only on success.
std::optional<ValidationError> applyPatch(const Config& base, const ConfigPatch& patch, Config& out);

}  // namespace jig
