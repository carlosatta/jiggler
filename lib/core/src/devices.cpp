#include "devices.h"

#include <cctype>
#include <set>

namespace jig {

namespace {

constexpr size_t kMaxDevices = 4;
constexpr size_t kMaxNameLen = 29;
constexpr size_t kMaxManufacturerLen = 32;
constexpr const char* kBlobKey = "devices";

const char* kindName(DeviceKind k) { return k == DeviceKind::Mouse ? "mouse" : "keyboard"; }

bool validId(const std::string& id) {
  if (id.empty() || id.size() > 24) return false;
  for (char c : id)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  return true;
}

bool noControlChars(const std::string& s) {
  for (unsigned char c : s)
    if (c < 0x20 || c == 0x7F) return false;
  return true;
}

std::string at(size_t i, const char* field) { return "devices[" + std::to_string(i) + "]." + field; }

std::string macToString(const std::array<uint8_t, 3>& oui) {
  char buf[9];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X", oui[0], oui[1], oui[2]);
  return buf;
}

bool parseFail(ParseError& err, const char* code, const std::string& field, const char* message) {
  err.code = code;
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

std::vector<DeviceConfig> defaultDevices() {
  DeviceConfig mouse;
  mouse.id = "mouse";
  mouse.kind = DeviceKind::Mouse;
  mouse.enabled = true;
  mouse.profileId = "logitech-mx-master-3s";
  return {mouse};
}

std::optional<ValidationError> validateDevices(const std::vector<DeviceConfig>& devices) {
  if (devices.empty() || devices.size() > kMaxDevices)
    return ValidationError{"devices", "must contain 1 to 4 devices"};

  std::set<std::string> ids;
  bool anyEnabled = false, anyMouse = false;
  for (size_t i = 0; i < devices.size(); i++) {
    const DeviceConfig& d = devices[i];
    if (d.kind == DeviceKind::Keyboard) return ValidationError{at(i, "kind"), "keyboards are not supported yet"};
    if (!validId(d.id)) return ValidationError{at(i, "id"), "must be 1 to 24 characters of a-z 0-9 _ -"};
    if (!ids.insert(d.id).second) return ValidationError{at(i, "id"), "duplicate id"};
    if (d.profileId != "custom") {
      const Profile* p = findProfile(d.profileId);
      if (!p) return ValidationError{at(i, "profileId"), "unknown profile"};
      if (p->kind != d.kind) return ValidationError{at(i, "profileId"), "profile is for a different device kind"};
    }
    if (d.name && (d.name->empty() || d.name->size() > kMaxNameLen || !noControlChars(*d.name)))
      return ValidationError{at(i, "name"), "must be 1 to 29 characters"};
    if (d.manufacturer && (d.manufacturer->empty() || d.manufacturer->size() > kMaxManufacturerLen || !noControlChars(*d.manufacturer)))
      return ValidationError{at(i, "manufacturer"), "must be 1 to 32 characters"};
    if (d.mac) {
      auto mac = parseMac(*d.mac);
      if (!mac) return ValidationError{at(i, "mac"), "must look like AA:BB:CC:DD:EE:FF"};
      if ((*mac)[0] & 1) return ValidationError{at(i, "mac"), "must be a unicast address (first byte even)"};
    }
    anyEnabled |= d.enabled;
    anyMouse |= d.enabled && d.kind == DeviceKind::Mouse;
  }
  if (!anyEnabled) return ValidationError{"devices", "at least one device must be enabled"};
  if (!anyMouse) return ValidationError{"devices", "at least one mouse must be enabled (the jiggler needs it)"};
  return std::nullopt;
}

std::string devicesToJson(const std::vector<DeviceConfig>& devices, std::optional<bool> restartRequired) {
  JsonDocument doc;
  JsonArray arr = doc["devices"].to<JsonArray>();
  for (const DeviceConfig& d : devices) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = d.id;
    o["kind"] = kindName(d.kind);
    o["enabled"] = d.enabled;
    o["profileId"] = d.profileId;
    if (d.name) o["name"] = *d.name;
    if (d.manufacturer) o["manufacturer"] = *d.manufacturer;
    if (d.vid) o["vid"] = *d.vid;
    if (d.pid) o["pid"] = *d.pid;
    if (d.mac) o["mac"] = *d.mac;
  }
  if (restartRequired) doc["restartRequired"] = *restartRequired;
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string profilesToJson() {
  JsonDocument doc;
  JsonArray arr = doc["profiles"].to<JsonArray>();
  for (const Profile& p : catalog()) {
    if (p.kind != DeviceKind::Mouse) continue;  // keyboards are not offered for now
    JsonObject o = arr.add<JsonObject>();
    o["id"] = p.id;
    o["label"] = p.label;
    o["kind"] = kindName(p.kind);
    o["name"] = p.identity.name;
    o["manufacturer"] = p.identity.manufacturer;
    o["vid"] = p.identity.vid;
    o["pid"] = p.identity.pid;
    if (p.identity.oui) o["oui"] = macToString(*p.identity.oui);
  }
  JsonObject custom = doc["custom"].to<JsonObject>();
  for (DeviceKind k : {DeviceKind::Mouse}) {
    Identity g = genericIdentity(k);
    JsonObject o = custom[kindName(k)].to<JsonObject>();
    o["name"] = g.name;
    o["manufacturer"] = g.manufacturer;
    o["vid"] = g.vid;
    o["pid"] = g.pid;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool parseDevices(JsonArrayConst array, std::vector<DeviceConfig>& out, ParseError& err) {
  std::vector<DeviceConfig> parsed;
  size_t i = 0;
  for (JsonVariantConst item : array) {
    if (!item.is<JsonObjectConst>()) return parseFail(err, "invalid_devices", at(i, "").substr(0, at(i, "").size() - 1), "must be an object");
    DeviceConfig d;
    bool hasId = false, hasKind = false, hasEnabled = false;
    for (JsonPairConst kv : item.as<JsonObjectConst>()) {
      const std::string key = kv.key().c_str();
      JsonVariantConst v = kv.value();
      const std::string field = at(i, key.c_str());
      if (key == "id") {
        if (!v.is<const char*>()) return parseFail(err, "invalid_devices", field, "must be a string");
        d.id = v.as<std::string>();
        hasId = true;
      } else if (key == "kind") {
        const std::string k = v.is<const char*>() ? v.as<std::string>() : "";
        if (k == "mouse") d.kind = DeviceKind::Mouse;
        else if (k == "keyboard") d.kind = DeviceKind::Keyboard;
        else return parseFail(err, "invalid_devices", field, "must be mouse or keyboard");
        hasKind = true;
      } else if (key == "enabled") {
        if (!v.is<bool>()) return parseFail(err, "invalid_devices", field, "must be true or false");
        d.enabled = v.as<bool>();
        hasEnabled = true;
      } else if (key == "profileId") {
        if (!v.is<const char*>()) return parseFail(err, "invalid_devices", field, "must be a string");
        d.profileId = v.as<std::string>();
      } else if (key == "name" || key == "manufacturer" || key == "mac") {
        if (v.isNull()) continue;
        if (!v.is<const char*>()) return parseFail(err, "invalid_devices", field, "must be a string");
        std::optional<std::string>& target = key == "name" ? d.name : key == "manufacturer" ? d.manufacturer : d.mac;
        target = v.as<std::string>();
      } else if (key == "vid" || key == "pid") {
        if (v.isNull()) continue;
        if (!v.is<int64_t>() || v.as<int64_t>() < 0 || v.as<int64_t>() > 65535)
          return parseFail(err, "invalid_devices", field, "must be an integer from 0 to 65535");
        (key == "vid" ? d.vid : d.pid) = static_cast<uint16_t>(v.as<int64_t>());
      } else {
        return parseFail(err, "invalid_devices", field, "unknown field");
      }
    }
    if (!hasId) return parseFail(err, "invalid_devices", at(i, "id"), "is required");
    if (!hasKind) return parseFail(err, "invalid_devices", at(i, "kind"), "is required");
    if (!hasEnabled) return parseFail(err, "invalid_devices", at(i, "enabled"), "is required");
    parsed.push_back(std::move(d));
    i++;
  }
  out = std::move(parsed);
  return true;
}

bool parseDevices(const std::string& body, std::vector<DeviceConfig>& out, ParseError& err) {
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc.is<JsonObject>())
    return parseFail(err, "bad_json", "", "body must be a JSON object");
  if (!doc["devices"].is<JsonArray>()) return parseFail(err, "invalid_devices", "devices", "must be an array");
  return parseDevices(doc["devices"].as<JsonArrayConst>(), out, err);
}

Identity resolveIdentity(const DeviceConfig& d) {
  const Profile* p = d.profileId == "custom" ? nullptr : findProfile(d.profileId);
  Identity id = p ? p->identity : genericIdentity(d.kind);
  if (d.name) id.name = *d.name;
  if (d.manufacturer) id.manufacturer = *d.manufacturer;
  if (d.vid) id.vid = *d.vid;
  if (d.pid) id.pid = *d.pid;
  return id;
}

std::optional<std::array<uint8_t, 6>> resolveMac(const DeviceConfig& d, const Identity& identity,
                                                 const uint8_t efuseMac[6]) {
  if (d.mac) return parseMac(*d.mac);
  if (identity.oui)
    return std::array<uint8_t, 6>{(*identity.oui)[0], (*identity.oui)[1], (*identity.oui)[2], efuseMac[3],
                                  efuseMac[4], efuseMac[5]};
  return std::nullopt;
}

DeviceRegistry::DeviceRegistry(BlobStore& blobs) : blobs_(blobs) {}

void DeviceRegistry::begin() {
  std::lock_guard<std::mutex> lock(mutex_);
  devices_ = defaultDevices();
  const std::string raw = blobs_.get(kBlobKey);
  if (raw.empty()) return;
  std::vector<DeviceConfig> stored;
  ParseError err;
  if (!parseDevices(raw, stored, err)) return;
  // Lists saved by older versions may contain a keyboard: drop it, keep the mouse.
  stored.erase(std::remove_if(stored.begin(), stored.end(), [](const DeviceConfig& d) { return d.kind == DeviceKind::Keyboard; }), stored.end());
  if (!stored.empty() && !validateDevices(stored)) devices_ = std::move(stored);
}

std::vector<DeviceConfig> DeviceRegistry::list() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return devices_;
}

CmdResult DeviceRegistry::replace(const std::vector<DeviceConfig>& devices) {
  CmdResult r;
  if (auto bad = validateDevices(devices)) {
    r.error = "invalid_devices";
    r.field = bad->field;
    r.message = bad->message;
    return r;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!blobs_.put(kBlobKey, devicesToJson(devices))) {
    r.error = "storage_error";
    r.message = "could not save the devices (storage full?)";
    return r;
  }
  devices_ = devices;
  r.ok = true;
  return r;
}

ResolvedDevice DeviceRegistry::resolved(const uint8_t efuseMac[6]) const {
  std::lock_guard<std::mutex> lock(mutex_);
  ResolvedDevice out;
  bool primarySet = false;
  for (const DeviceConfig& d : devices_) {
    if (!d.enabled) continue;
    out.mouse |= d.kind == DeviceKind::Mouse;
    out.keyboard |= d.kind == DeviceKind::Keyboard;
    if (!primarySet) {
      out.identity = resolveIdentity(d);
      out.mac = resolveMac(d, out.identity, efuseMac);
      out.macExplicit = d.mac.has_value();
      primarySet = true;
    }
  }
  return out;
}

}  // namespace jig
