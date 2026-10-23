#include "identity.h"

namespace jig {

std::array<uint8_t, 6> deriveLogitechMac(const uint8_t efuseMac[6]) {
  return {kLogitechOui[0], kLogitechOui[1], kLogitechOui[2], efuseMac[3], efuseMac[4], efuseMac[5]};
}

}  // namespace jig
