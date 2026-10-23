#include "config.h"

namespace jig {

namespace {
constexpr int64_t kMinIntervalSec = 5;
constexpr int64_t kMaxIntervalSec = 3600;
constexpr int64_t kMinAmplitudePx = 5;
constexpr int64_t kMaxAmplitudePx = 600;

bool inRange(int64_t v, int64_t lo, int64_t hi) { return v >= lo && v <= hi; }
bool isProbability(double v) { return v >= 0.0 && v <= 1.0; }  // false for NaN
}  // namespace

std::optional<ValidationError> applyPatch(const Config& base, const ConfigPatch& patch, Config& out) {
  const int64_t minSec = patch.minIntervalSec.value_or(base.minIntervalSec);
  const int64_t maxSec = patch.maxIntervalSec.value_or(base.maxIntervalSec);
  const int64_t amp = patch.amplitudePx.value_or(base.amplitudePx);
  const double click = patch.clickProbability.value_or(base.clickProbability);
  const double scroll = patch.scrollProbability.value_or(base.scrollProbability);

  if (!inRange(minSec, kMinIntervalSec, kMaxIntervalSec))
    return ValidationError{"minIntervalSec", "must be between 5 and 3600"};
  if (!inRange(maxSec, kMinIntervalSec, kMaxIntervalSec))
    return ValidationError{"maxIntervalSec", "must be between 5 and 3600"};
  if (!inRange(amp, kMinAmplitudePx, kMaxAmplitudePx))
    return ValidationError{"amplitudePx", "must be between 5 and 600"};
  if (!isProbability(click))
    return ValidationError{"clickProbability", "must be between 0 and 1"};
  if (!isProbability(scroll))
    return ValidationError{"scrollProbability", "must be between 0 and 1"};
  if (maxSec < minSec) {
    return ValidationError{patch.maxIntervalSec ? "maxIntervalSec" : "minIntervalSec",
                           "maxIntervalSec must be >= minIntervalSec"};
  }

  out.minIntervalSec = static_cast<uint32_t>(minSec);
  out.maxIntervalSec = static_cast<uint32_t>(maxSec);
  out.amplitudePx = static_cast<uint16_t>(amp);
  out.clickProbability = static_cast<float>(click);
  out.scrollProbability = static_cast<float>(scroll);
  return std::nullopt;
}

}  // namespace jig
