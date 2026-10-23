#include "network_settings.h"

namespace jig {

namespace {

bool printableAscii(const std::string& s) {
  for (unsigned char c : s)
    if (c < 0x20 || c > 0x7E) return false;
  return true;
}

bool fail(ParseError& err, const char* code, const std::string& field, const char* message) {
  err.code = code;
  err.field = field;
  err.message = message;
  return false;
}

}  // namespace

std::optional<ValidationError> validateNetwork(const NetworkSettings& s) {
  if (s.apSsid.empty() || s.apSsid.size() > 32)
    return ValidationError{"apSsid", "must be 1 to 32 characters"};
  for (unsigned char c : s.apSsid)
    if (c < 0x20) return ValidationError{"apSsid", "must not contain control characters"};
  if (!s.apPassword.empty() && (s.apPassword.size() < 8 || s.apPassword.size() > 63 || !printableAscii(s.apPassword)))
    return ValidationError{"apPassword", "must be empty or 8 to 63 printable ASCII characters"};
  return std::nullopt;
}

std::string networkToJson(const NetworkSettings& s) {
  JsonDocument doc;
  doc["mode"] = s.mode == NetworkMode::Ap ? "ap" : "auto";
  doc["apSsid"] = s.apSsid;
  doc["apPassword"] = s.apPassword;
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool parseNetworkPatch(const std::string& body, NetworkSettings& base, ParseError& err) {
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc.is<JsonObject>())
    return fail(err, "bad_json", "", "body must be a JSON object");

  NetworkSettings next = base;
  for (JsonPair kv : doc.as<JsonObject>()) {
    const std::string key = kv.key().c_str();
    JsonVariant v = kv.value();
    if (key == "mode") {
      if (!v.is<const char*>()) return fail(err, "invalid_network", key, "must be a string");
      const std::string m = v.as<std::string>();
      if (m == "auto") next.mode = NetworkMode::Auto;
      else if (m == "ap") next.mode = NetworkMode::Ap;
      else return fail(err, "invalid_network", key, "must be auto or ap");
    } else if (key == "apSsid" || key == "apPassword") {
      if (!v.is<const char*>()) return fail(err, "invalid_network", key, "must be a string");
      (key == "apSsid" ? next.apSsid : next.apPassword) = v.as<std::string>();
    } else {
      return fail(err, "invalid_network", key, "unknown field");
    }
  }
  if (auto bad = validateNetwork(next)) return fail(err, "invalid_network", bad->field, bad->message.c_str());
  base = next;
  return true;
}

bool isCaptiveProbePath(const std::string& path) {
  static const char* const kPaths[] = {"/generate_204", "/gen_204",        "/hotspot-detect.html",
                                       "/library/test/success.html", "/ncsi.txt", "/connecttest.txt",
                                       "/redirect",     "/canonical.html", "/success.txt"};
  for (const char* p : kPaths)
    if (path == p) return true;
  return false;
}

}  // namespace jig
