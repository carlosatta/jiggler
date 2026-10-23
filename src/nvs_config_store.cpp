#include "nvs_config_store.h"

#include <Preferences.h>

namespace {
constexpr const char* kNamespace = "jiggler";
}

jig::Config NvsConfigStore::load() {
  jig::Config c;
  Preferences prefs;
  prefs.begin(kNamespace, false);
  c.minIntervalSec = prefs.getUInt("minSec", c.minIntervalSec);
  c.maxIntervalSec = prefs.getUInt("maxSec", c.maxIntervalSec);
  c.amplitudePx = prefs.getUShort("ampPx", c.amplitudePx);
  c.clickProbability = prefs.getFloat("clickP", c.clickProbability);
  c.scrollProbability = prefs.getFloat("scrollP", c.scrollProbability);
  prefs.end();
  return c;
}

void NvsConfigStore::save(const jig::Config& c) {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putUInt("minSec", c.minIntervalSec);
  prefs.putUInt("maxSec", c.maxIntervalSec);
  prefs.putUShort("ampPx", c.amplitudePx);
  prefs.putFloat("clickP", c.clickProbability);
  prefs.putFloat("scrollP", c.scrollProbability);
  prefs.end();
}
