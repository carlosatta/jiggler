#include "appearance.h"

#include <cctype>

namespace jig {

namespace {

constexpr size_t kMaxNameLen = 29;
constexpr size_t kMaxManufacturerLen = 32;
constexpr const char* kBlobKey = "appearance";

bool noControlChars(const std::string& s) {
  for (unsigned char c : s)
    if (c < 0x20 || c == 0x7F) return false;
  return true;
}

std::string ouiToString(const std::array<uint8_t, 3>& oui) {
  char buf[9];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X", oui[0], oui[1], oui[2]);
  return buf;
}

bool parseFail(ParseError& err, const std::string& field, const char* message) {
  err.code = field.empty() ? "bad_json" : "invalid_appearance";
  err.field = field;
  err.message = message;
  return false;
}

}  // namespace

std::optional<std::array<uint8_t, 6>> parseMac(const std::string& text) {
  if (text.size() != 17) return std::nullopt;
  std::array<uint8_t, 6> mac{};
  for (size_t i = 0; i < 6; i++) {
    const size_t p = i * 3;
    if (i > 0 && text[p - 1] != ':') return std::nullopt;
    int value = 0;
    for (size_t j = 0; j < 2; j++) {
      const char c = text[p + j];
      int digit;
      if (c >= '0' && c <= '9') digit = c - '0';
      else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
      else return std::nullopt;
      value = value * 16 + digit;
    }
    mac[i] = static_cast<uint8_t>(value);
  }
  return mac;
}


std::optional<ValidationError> validateAppearance(const Appearance& a) {
  if (a.profile != "custom" && !findProfile(a.profile)) return ValidationError{"profile", "unknown profile"};
  if (a.name && (a.name->empty() || a.name->size() > kMaxNameLen || !noControlChars(*a.name)))
    return ValidationError{"name", "must be 1 to 29 characters"};
  if (a.manufacturer && (a.manufacturer->empty() || a.manufacturer->size() > kMaxManufacturerLen || !noControlChars(*a.manufacturer)))
    return ValidationError{"manufacturer", "must be 1 to 32 characters"};
  if (a.mac) {
    auto mac = parseMac(*a.mac);
    if (!mac) return ValidationError{"mac", "must look like AA:BB:CC:DD:EE:FF"};
    if ((*mac)[0] & 1) return ValidationError{"mac", "must be a unicast address (first byte even)"};
  }
  return std::nullopt;
}

std::string appearanceToJson(const Appearance& a, std::optional<bool> restartRequired, bool withCatalog) {
  JsonDocument doc;
  doc["profile"] = a.profile;
  if (a.name) doc["name"] = *a.name;
  if (a.manufacturer) doc["manufacturer"] = *a.manufacturer;
  if (a.vid) doc["vid"] = *a.vid;
  if (a.pid) doc["pid"] = *a.pid;
  if (a.mac) doc["mac"] = *a.mac;
  if (withCatalog) {
    doc["shownAs"] = resolveIdentity(a).name;
    JsonArray arr = doc["profiles"].to<JsonArray>();
    for (const Profile& p : catalog()) {
      JsonObject o = arr.add<JsonObject>();
      o["id"] = p.id;
      o["label"] = p.label;
      o["name"] = p.identity.name;
      o["manufacturer"] = p.identity.manufacturer;
      o["vid"] = p.identity.vid;
      o["pid"] = p.identity.pid;
      if (p.identity.oui) o["oui"] = ouiToString(*p.identity.oui);
    }
    const Identity g = genericIdentity(DeviceKind::Mouse);
    JsonObject custom = doc["custom"].to<JsonObject>();
    custom["name"] = g.name;
    custom["manufacturer"] = g.manufacturer;
    custom["vid"] = g.vid;
    custom["pid"] = g.pid;
  }
  if (restartRequired) doc["restartRequired"] = *restartRequired;
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool parseAppearance(const std::string& body, Appearance& out, ParseError& err) {
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc.is<JsonObject>()) return parseFail(err, "", "body must be a JSON object");
  Appearance parsed;
  for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
    const std::string key = kv.key().c_str();
    JsonVariantConst v = kv.value();
    if (key == "shownAs" || key == "profiles" || key == "custom" || key == "restartRequired") continue;  // read-only
    if (key == "profile") {
      if (!v.is<const char*>()) return parseFail(err, key, "must be a string");
      parsed.profile = v.as<std::string>();
    } else if (key == "name" || key == "manufacturer" || key == "mac") {
      if (v.isNull()) continue;
      if (!v.is<const char*>()) return parseFail(err, key, "must be a string");
      (key == "name" ? parsed.name : key == "manufacturer" ? parsed.manufacturer : parsed.mac) = v.as<std::string>();
    } else if (key == "vid" || key == "pid") {
      if (v.isNull()) continue;
      if (!v.is<int64_t>() || v.as<int64_t>() < 0 || v.as<int64_t>() > 65535)
        return parseFail(err, key, "must be an integer from 0 to 65535");
      (key == "vid" ? parsed.vid : parsed.pid) = static_cast<uint16_t>(v.as<int64_t>());
    } else {
      return parseFail(err, key, "unknown field");
    }
  }
  out = std::move(parsed);
  return true;
}

Identity resolveIdentity(const Appearance& a) {
  const Profile* p = a.profile == "custom" ? nullptr : findProfile(a.profile);
  Identity id = p ? p->identity : genericIdentity(DeviceKind::Mouse);
  if (a.name) id.name = *a.name;
  if (a.manufacturer) id.manufacturer = *a.manufacturer;
  if (a.vid) id.vid = *a.vid;
  if (a.pid) id.pid = *a.pid;
  return id;
}

std::optional<std::array<uint8_t, 6>> resolveMac(const Appearance& a, const Identity& identity, const uint8_t efuseMac[6]) {
  if (a.mac) return parseMac(*a.mac);
  if (identity.oui)
    return std::array<uint8_t, 6>{(*identity.oui)[0], (*identity.oui)[1], (*identity.oui)[2], efuseMac[3], efuseMac[4], efuseMac[5]};
  return std::nullopt;
}

AppearanceStore::AppearanceStore(BlobStore& blobs) : blobs_(blobs) {}

void AppearanceStore::begin() {
  std::lock_guard<std::mutex> lock(mutex_);
  appearance_ = Appearance{};
  const std::string raw = blobs_.get(kBlobKey);
  if (raw.empty()) return;
  Appearance stored;
  ParseError err;
  if (parseAppearance(raw, stored, err) && !validateAppearance(stored)) appearance_ = std::move(stored);
}

Appearance AppearanceStore::get() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return appearance_;
}

CmdResult AppearanceStore::set(const Appearance& a) {
  CmdResult r;
  if (auto bad = validateAppearance(a)) {
    r.error = "invalid_appearance";
    r.field = bad->field;
    r.message = bad->message;
    return r;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!blobs_.put(kBlobKey, appearanceToJson(a, std::nullopt, false))) {
    r.error = "storage_error";
    r.message = "could not save the appearance (storage full?)";
    return r;
  }
  appearance_ = a;
  r.ok = true;
  return r;
}

ResolvedAppearance AppearanceStore::resolved(const uint8_t efuseMac[6]) const {
  std::lock_guard<std::mutex> lock(mutex_);
  ResolvedAppearance out;
  out.identity = resolveIdentity(appearance_);
  out.mac = resolveMac(appearance_, out.identity, efuseMac);
  out.macExplicit = appearance_.mac.has_value();
  return out;
}

}  // namespace jig
