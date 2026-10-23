#include "nvs_config_store.h"

#include <Preferences.h>

namespace {
constexpr const char* kNamespace = "jiggler";
}

jig::Config NvsConfigStore::load() {
  jig::Config c;
  Preferences prefs;
  prefs.begin(kNamespace, false);
  if (prefs.isKey("minSec")) c.minIntervalSec = prefs.getUInt("minSec");
  if (prefs.isKey("maxSec")) c.maxIntervalSec = prefs.getUInt("maxSec");
  if (prefs.isKey("ampPx")) c.amplitudePx = prefs.getUShort("ampPx");
  if (prefs.isKey("clickP")) c.clickProbability = prefs.getFloat("clickP");
  if (prefs.isKey("scrollP")) c.scrollProbability = prefs.getFloat("scrollP");
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
