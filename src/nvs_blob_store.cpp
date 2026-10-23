#include "nvs_blob_store.h"

#include <Preferences.h>

#include <algorithm>

namespace {

constexpr const char* kNamespace = "jiggler";
constexpr const char* kIndexKey = "_idx";

std::string nvsKey(const std::string& key) {
  uint32_t h = 2166136261u;  // FNV-1a
  for (unsigned char c : key) {
    h ^= c;
    h *= 16777619u;
  }
  char buf[10];
  snprintf(buf, sizeof(buf), "b%08x", static_cast<unsigned>(h));
  return buf;
}

std::vector<std::string> readIndex(Preferences& prefs) {
  std::vector<std::string> out;
  if (!prefs.isKey(kIndexKey)) return out;
  const std::string raw = prefs.getString(kIndexKey).c_str();
  size_t start = 0;
  while (start <= raw.size()) {
    const size_t nl = raw.find('\n', start);
    const std::string item = raw.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
    if (!item.empty()) out.push_back(item);
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  return out;
}

void writeIndex(Preferences& prefs, const std::vector<std::string>& keys) {
  std::string raw;
  for (const std::string& k : keys) raw += k + "\n";
  prefs.putString(kIndexKey, raw.c_str());
}

}  // namespace

std::string NvsBlobStore::get(const std::string& key) {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  const std::string nk = nvsKey(key);
  const std::string value = prefs.isKey(nk.c_str()) ? prefs.getString(nk.c_str()).c_str() : "";
  prefs.end();
  return value;
}

bool NvsBlobStore::put(const std::string& key, const std::string& value) {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  const std::string nk = nvsKey(key);
  // putString returns the number of bytes written, 0 when NVS refused the write (e.g. no free page).
  const size_t written = prefs.putString(nk.c_str(), value.c_str());
  bool ok = value.empty() || written == value.size();
  if (ok) {
    std::vector<std::string> index = readIndex(prefs);
    if (std::find(index.begin(), index.end(), key) == index.end()) {
      index.push_back(key);
      std::string raw;
      for (const std::string& k : index) raw += k + "\n";
      ok = prefs.putString(kIndexKey, raw.c_str()) == raw.size();
      if (!ok) prefs.remove(nk.c_str());  // never leave a value that the index does not know about
    }
  }
  prefs.end();
  return ok;
}

void NvsBlobStore::remove(const std::string& key) {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.remove(nvsKey(key).c_str());
  std::vector<std::string> index = readIndex(prefs);
  index.erase(std::remove(index.begin(), index.end(), key), index.end());
  writeIndex(prefs, index);
  prefs.end();
}

std::vector<std::string> NvsBlobStore::keys(const std::string& prefix) {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  std::vector<std::string> all = readIndex(prefs);
  prefs.end();
  std::vector<std::string> out;
  for (const std::string& k : all)
    if (k.rfind(prefix, 0) == 0) out.push_back(k);
  return out;
}
