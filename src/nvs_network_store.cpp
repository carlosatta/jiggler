#include "nvs_network_store.h"

#include <Preferences.h>

namespace {
constexpr const char* kNamespace = "jiggler";
}

jig::NetworkSettings NvsNetworkStore::load() {
  jig::NetworkSettings s;
  Preferences prefs;
  prefs.begin(kNamespace, false);
  if (prefs.isKey("netMode")) s.mode = prefs.getUChar("netMode") == 1 ? jig::NetworkMode::Ap : jig::NetworkMode::Auto;
  if (prefs.isKey("apSsid")) s.apSsid = prefs.getString("apSsid").c_str();
  if (prefs.isKey("apPass")) s.apPassword = prefs.getString("apPass").c_str();
  prefs.end();
  return jig::validateNetwork(s) ? jig::NetworkSettings{} : s;  // corrupt NVS falls back to defaults
}

void NvsNetworkStore::save(const jig::NetworkSettings& s) {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putUChar("netMode", s.mode == jig::NetworkMode::Ap ? 1 : 0);
  prefs.putString("apSsid", s.apSsid.c_str());
  prefs.putString("apPass", s.apPassword.c_str());
  prefs.end();
}
