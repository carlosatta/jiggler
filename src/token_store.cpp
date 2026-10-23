#include "token_store.h"

#include <Preferences.h>

namespace {
constexpr const char* kNamespace = "jiggler";
constexpr const char* kKey = "apitoken";
}  // namespace

std::string TokenStore::load() {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  const String token = prefs.isKey(kKey) ? prefs.getString(kKey, "") : String();
  prefs.end();
  return token.c_str();
}

bool TokenStore::save(const std::string& token) {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  const size_t written = prefs.putString(kKey, token.c_str());
  prefs.end();
  return written == token.size();
}
