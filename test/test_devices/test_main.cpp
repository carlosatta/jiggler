#include <unity.h>

#include <ArduinoJson.h>

#include <set>
#include <string>

#include "api_ex.h"
#include "controller.h"
#include "devices.h"
#include "fakes.h"
#include "profiles.h"

using namespace jig;

void setUp() {}
void tearDown() {}

namespace {

const uint8_t kEfuse[6] = {0x01, 0x02, 0x03, 0xAA, 0xBB, 0xCC};

JsonDocument parse(const std::string& s) {
  JsonDocument doc;
  TEST_ASSERT_TRUE_MESSAGE(!deserializeJson(doc, s), s.c_str());
  return doc;
}

DeviceConfig dev(const char* id, DeviceKind kind, bool enabled, const char* profile) {
  DeviceConfig d;
  d.id = id;
  d.kind = kind;
  d.enabled = enabled;
  d.profileId = profile;
  return d;
}

struct Api {
  FakeClock clock;
  XorShiftRng rng{1};
  MemStore config;
  Controller controller{clock, rng, config};
  MemBlobStore blobs;
  DeviceRegistry registry{blobs};
  ApiContext ctx{controller};

  Api() {
    controller.begin();
    registry.begin();
    ctx.devices = &registry;
  }
  HttpResponse call(const std::string& method, const std::string& path, const std::string& body = "") {
    return handleApiEx(ctx, method, path, body);
  }
};

}  // namespace

void test_catalog_has_verified_logitech_default() {
  const Profile* p = findProfile("logitech-mx-master-3s");
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_TRUE(p->kind == DeviceKind::Mouse);
  TEST_ASSERT_EQUAL_STRING("MX Master 3S", p->identity.name.c_str());
  TEST_ASSERT_EQUAL_STRING("Logitech", p->identity.manufacturer.c_str());
  TEST_ASSERT_EQUAL_HEX16(0x046D, p->identity.vid);
  TEST_ASSERT_EQUAL_HEX16(0xB034, p->identity.pid);
  TEST_ASSERT_TRUE(p->identity.oui.has_value());
  TEST_ASSERT_EQUAL_HEX8(0x00, (*p->identity.oui)[0]);
  TEST_ASSERT_EQUAL_HEX8(0x1F, (*p->identity.oui)[1]);
  TEST_ASSERT_EQUAL_HEX8(0x20, (*p->identity.oui)[2]);
}

void test_catalog_entries_pin_verified_values() {
  const struct {
    const char* id;
    DeviceKind kind;
    uint16_t vid, pid;
    uint8_t oui0, oui1, oui2;
  } expected[] = {
      {"logitech-mx-keys", DeviceKind::Keyboard, 0x046D, 0xB35B, 0x00, 0x1F, 0x20},
      {"razer-orochi-v2", DeviceKind::Mouse, 0x1532, 0x0095, 0x44, 0x5E, 0xCD},
      {"apple-magic-mouse-2", DeviceKind::Mouse, 0x004C, 0x0269, 0x58, 0xAD, 0x12},
      {"apple-magic-keyboard", DeviceKind::Keyboard, 0x004C, 0x0267, 0x58, 0xAD, 0x12},
  };
  for (const auto& e : expected) {
    const Profile* p = findProfile(e.id);
    TEST_ASSERT_NOT_NULL_MESSAGE(p, e.id);
    TEST_ASSERT_TRUE_MESSAGE(p->kind == e.kind, e.id);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(e.vid, p->identity.vid, e.id);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(e.pid, p->identity.pid, e.id);
    TEST_ASSERT_TRUE_MESSAGE(p->identity.oui.has_value(), e.id);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(e.oui0, (*p->identity.oui)[0], e.id);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(e.oui1, (*p->identity.oui)[1], e.id);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(e.oui2, (*p->identity.oui)[2], e.id);
  }
}

void test_catalog_ids_unique_and_wellformed() {
  std::set<std::string> seen;
  for (const Profile& p : catalog()) {
    TEST_ASSERT_TRUE_MESSAGE(seen.insert(p.id).second, p.id.c_str());
    TEST_ASSERT_TRUE_MESSAGE(!p.identity.name.empty() && p.identity.name.size() <= 29, p.id.c_str());
    TEST_ASSERT_TRUE_MESSAGE(!p.identity.manufacturer.empty() && p.identity.manufacturer.size() <= 32, p.id.c_str());
    TEST_ASSERT_TRUE_MESSAGE(p.id != "custom", p.id.c_str());
    TEST_ASSERT_TRUE_MESSAGE(p.identity.vidSource == 1 || p.identity.vidSource == 2, p.id.c_str());
    if (p.identity.oui) TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, (*p.identity.oui)[0] & 1, p.id.c_str());  // unicast
    for (char c : p.id) TEST_ASSERT_TRUE_MESSAGE((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-', p.id.c_str());
  }
  TEST_ASSERT_TRUE(catalog().size() >= 5);
}

void test_default_devices() {
  auto d = defaultDevices();
  TEST_ASSERT_EQUAL_UINT32(2, d.size());
  TEST_ASSERT_EQUAL_STRING("mouse", d[0].id.c_str());
  TEST_ASSERT_TRUE(d[0].kind == DeviceKind::Mouse && d[0].enabled);
  TEST_ASSERT_EQUAL_STRING("logitech-mx-master-3s", d[0].profileId.c_str());
  TEST_ASSERT_EQUAL_STRING("keyboard", d[1].id.c_str());
  TEST_ASSERT_TRUE(d[1].kind == DeviceKind::Keyboard && !d[1].enabled);
  TEST_ASSERT_EQUAL_STRING("custom", d[1].profileId.c_str());
  TEST_ASSERT_FALSE(validateDevices(d).has_value());
}

void test_resolve_identity_overrides() {
  DeviceConfig d = dev("mouse", DeviceKind::Mouse, true, "logitech-mx-master-3s");
  d.name = "Foo";
  d.vid = 0x1234;
  Identity id = resolveIdentity(d);
  TEST_ASSERT_EQUAL_STRING("Foo", id.name.c_str());
  TEST_ASSERT_EQUAL_STRING("Logitech", id.manufacturer.c_str());
  TEST_ASSERT_EQUAL_HEX16(0x1234, id.vid);
  TEST_ASSERT_EQUAL_HEX16(0xB034, id.pid);
}

void test_resolve_identity_custom_defaults() {
  Identity m = resolveIdentity(dev("m", DeviceKind::Mouse, true, "custom"));
  TEST_ASSERT_EQUAL_STRING("Bluetooth Mouse", m.name.c_str());
  TEST_ASSERT_EQUAL_STRING("Generic", m.manufacturer.c_str());
  TEST_ASSERT_FALSE(m.oui.has_value());
  Identity k = resolveIdentity(dev("k", DeviceKind::Keyboard, true, "custom"));
  TEST_ASSERT_EQUAL_STRING("Bluetooth Keyboard", k.name.c_str());
  TEST_ASSERT_EQUAL_HEX16(0x03C1, k.appearance);
  TEST_ASSERT_EQUAL_HEX16(0x03C2, m.appearance);
}

void test_resolve_mac() {
  DeviceConfig d = dev("mouse", DeviceKind::Mouse, true, "logitech-mx-master-3s");
  auto mac = resolveMac(d, resolveIdentity(d), kEfuse);
  TEST_ASSERT_TRUE(mac.has_value());
  const uint8_t logi[6] = {0x00, 0x1F, 0x20, 0xAA, 0xBB, 0xCC};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(logi, mac->data(), 6);

  d.mac = "02:11:22:33:44:55";
  auto custom = resolveMac(d, resolveIdentity(d), kEfuse);
  const uint8_t over[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(over, custom->data(), 6);

  DeviceConfig c = dev("m", DeviceKind::Mouse, true, "custom");
  TEST_ASSERT_FALSE(resolveMac(c, resolveIdentity(c), kEfuse).has_value());

  DeviceConfig r = dev("m", DeviceKind::Mouse, true, "razer-orochi-v2");
  const uint8_t razer[6] = {0x44, 0x5E, 0xCD, 0xAA, 0xBB, 0xCC};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(razer, resolveMac(r, resolveIdentity(r), kEfuse)->data(), 6);
}

void test_parse_mac() {
  TEST_ASSERT_TRUE(parseMac("00:1f:20:AA:bb:0C").has_value());
  for (const char* bad : {"", "00:1F:20:AA:BB", "00:1F:20:AA:BB:CC:DD", "zz:1F:20:AA:BB:CC", "001F20AABBCC",
                          "00-1F-20-AA-BB-CC", "01:1F:20:AA:BB:CC" /* multicast is checked by validate, not parse */})
    if (std::string(bad) == "01:1F:20:AA:BB:CC") TEST_ASSERT_TRUE(parseMac(bad).has_value());
    else TEST_ASSERT_FALSE_MESSAGE(parseMac(bad).has_value(), bad);
}

void test_validate_devices_table() {  // RF1
  struct Case {
    const char* what;
    std::vector<DeviceConfig> devices;
  };
  std::vector<Case> cases;
  auto base = [] { return defaultDevices(); };

  { auto d = base(); d[0].profileId = "nope"; cases.push_back({"unknown profile", d}); }
  { auto d = base(); d[0].profileId = "logitech-mx-keys"; cases.push_back({"kind mismatch", d}); }
  { auto d = base(); d[1].id = "mouse"; cases.push_back({"duplicate ids", d}); }
  { auto d = base(); d[0].id = "Bad Id"; cases.push_back({"bad id", d}); }
  { auto d = base(); d[0].id = ""; cases.push_back({"empty id", d}); }
  { auto d = base(); d[0].mac = "00:1F:20:AA:BB"; cases.push_back({"5-octet mac", d}); }
  { auto d = base(); d[0].mac = "zz:1F:20:AA:BB:CC"; cases.push_back({"non-hex mac", d}); }
  { auto d = base(); d[0].mac = "01:1F:20:AA:BB:CC"; cases.push_back({"multicast mac", d}); }
  { auto d = base(); d[0].name = std::string(30, 'n'); cases.push_back({"name too long", d}); }
  { auto d = base(); d[0].name = ""; cases.push_back({"empty name", d}); }
  { auto d = base(); d[0].manufacturer = std::string(33, 'm'); cases.push_back({"manufacturer too long", d}); }
  { auto d = base(); d[0].enabled = false; cases.push_back({"none enabled", d}); }
  { std::vector<DeviceConfig> d; cases.push_back({"empty list", d}); }
  { std::vector<DeviceConfig> d; for (int i = 0; i < 5; i++) d.push_back(dev(("d" + std::to_string(i)).c_str(), DeviceKind::Mouse, true, "custom")); cases.push_back({"5 devices", d}); }

  for (const Case& c : cases) TEST_ASSERT_TRUE_MESSAGE(validateDevices(c.devices).has_value(), c.what);

  auto ok = base();
  ok[0].mac = "02:11:22:33:44:55";
  ok[0].name = std::string(29, 'n');
  TEST_ASSERT_FALSE(validateDevices(ok).has_value());
  auto four = std::vector<DeviceConfig>{dev("a", DeviceKind::Mouse, true, "custom"), dev("b", DeviceKind::Mouse, false, "custom"),
                                        dev("c", DeviceKind::Keyboard, false, "custom"), dev("d", DeviceKind::Keyboard, false, "custom")};
  TEST_ASSERT_FALSE(validateDevices(four).has_value());
}

void test_json_roundtrip() {
  auto d = defaultDevices();
  d[0].name = "Mio Mouse";
  d[0].mac = "02:11:22:33:44:55";
  d[0].vid = 0x1234;
  d[1].enabled = true;
  std::vector<DeviceConfig> back;
  ParseError err;
  TEST_ASSERT_TRUE(parseDevices(devicesToJson(d), back, err));
  TEST_ASSERT_EQUAL_UINT32(2, back.size());
  TEST_ASSERT_EQUAL_STRING("Mio Mouse", back[0].name->c_str());
  TEST_ASSERT_EQUAL_STRING("02:11:22:33:44:55", back[0].mac->c_str());
  TEST_ASSERT_EQUAL_HEX16(0x1234, *back[0].vid);
  TEST_ASSERT_FALSE(back[0].pid.has_value());
  TEST_ASSERT_TRUE(back[1].enabled);
  TEST_ASSERT_TRUE(back[1].kind == DeviceKind::Keyboard);
}

void test_parse_devices_bad_input() {  // RF1
  const char* bodies[] = {
      "",
      "{",
      "[]",
      "{}",
      "{\"devices\":5}",
      "{\"devices\":[5]}",
      "{\"devices\":[{\"kind\":\"mouse\",\"enabled\":true}]}",
      "{\"devices\":[{\"id\":\"a\",\"kind\":\"gamepad\",\"enabled\":true}]}",
      "{\"devices\":[{\"id\":\"a\",\"kind\":\"mouse\",\"enabled\":\"yes\"}]}",
      "{\"devices\":[{\"id\":\"a\",\"kind\":\"mouse\",\"enabled\":true,\"vid\":70000}]}",
      "{\"devices\":[{\"id\":\"a\",\"kind\":\"mouse\",\"enabled\":true,\"vid\":-1}]}",
      "{\"devices\":[{\"id\":\"a\",\"kind\":\"mouse\",\"enabled\":true,\"pid\":1.5}]}",
      "{\"devices\":[{\"id\":\"a\",\"kind\":\"mouse\",\"enabled\":true,\"name\":5}]}",
      "{\"devices\":[{\"id\":\"a\",\"kind\":\"mouse\",\"enabled\":true,\"bogus\":1}]}",
  };
  for (const char* b : bodies) {
    std::vector<DeviceConfig> out;
    ParseError err;
    TEST_ASSERT_FALSE_MESSAGE(parseDevices(b, out, err), b);
    TEST_ASSERT_TRUE_MESSAGE(out.empty(), b);
  }
}

void test_registry_primary_and_flags() {
  MemBlobStore blobs;
  DeviceRegistry reg(blobs);
  reg.begin();
  ResolvedDevice r = reg.resolved(kEfuse);
  TEST_ASSERT_TRUE(r.mouse);
  TEST_ASSERT_FALSE(r.keyboard);
  TEST_ASSERT_EQUAL_STRING("MX Master 3S", r.identity.name.c_str());
  TEST_ASSERT_TRUE(r.mac.has_value());

  auto d = defaultDevices();
  d[1].enabled = true;
  TEST_ASSERT_TRUE(reg.replace(d).ok);
  r = reg.resolved(kEfuse);
  TEST_ASSERT_TRUE(r.mouse && r.keyboard);
  TEST_ASSERT_EQUAL_STRING("MX Master 3S", r.identity.name.c_str());  // first enabled device is primary

  d[0].enabled = false;
  d[1].profileId = "logitech-mx-keys";
  TEST_ASSERT_TRUE(reg.replace(d).ok);
  r = reg.resolved(kEfuse);
  TEST_ASSERT_FALSE(r.mouse);
  TEST_ASSERT_TRUE(r.keyboard);
  TEST_ASSERT_EQUAL_STRING("MX Keys", r.identity.name.c_str());
  TEST_ASSERT_EQUAL_HEX16(0xB35B, r.identity.pid);
}

void test_registry_replace_persists_and_invalid_changes_nothing() {  // RF1
  MemBlobStore blobs;
  {
    DeviceRegistry reg(blobs);
    reg.begin();
    auto d = defaultDevices();
    d[0].name = "Persistito";
    CmdResult ok = reg.replace(d);
    TEST_ASSERT_TRUE(ok.ok);

    auto bad = defaultDevices();
    bad[0].mac = "zz";
    CmdResult r = reg.replace(bad);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("invalid_devices", r.error.c_str());
    TEST_ASSERT_EQUAL_STRING("devices[0].mac", r.field.c_str());
    TEST_ASSERT_EQUAL_STRING("Persistito", reg.list()[0].name->c_str());
  }
  DeviceRegistry again(blobs);
  again.begin();
  TEST_ASSERT_EQUAL_STRING("Persistito", again.list()[0].name->c_str());
}

void test_registry_corrupt_blob_falls_back_to_defaults() {
  MemBlobStore blobs;
  blobs.put("devices", "{not json");
  DeviceRegistry reg(blobs);
  reg.begin();
  TEST_ASSERT_EQUAL_UINT32(2, reg.list().size());
  TEST_ASSERT_EQUAL_STRING("logitech-mx-master-3s", reg.list()[0].profileId.c_str());
  blobs.put("devices", "{\"devices\":[{\"id\":\"a\",\"kind\":\"mouse\",\"enabled\":false}]}");  // parses but invalid (none enabled)
  DeviceRegistry reg2(blobs);
  reg2.begin();
  TEST_ASSERT_EQUAL_STRING("mouse", reg2.list()[0].id.c_str());
}

void test_api_profiles_devices_routes() {
  Api a;
  HttpResponse p = a.call("GET", "/api/profiles");
  TEST_ASSERT_EQUAL_INT(200, p.status);
  JsonDocument pd = parse(p.body);
  TEST_ASSERT_TRUE(pd["profiles"].as<JsonArray>().size() >= 5);
  JsonObject first = pd["profiles"][0];
  TEST_ASSERT_TRUE(first["id"].is<const char*>() && first["kind"].is<const char*>() && first["name"].is<const char*>());
  TEST_ASSERT_TRUE(first["vid"].is<int>() && first["pid"].is<int>());

  HttpResponse g = a.call("GET", "/api/devices");
  TEST_ASSERT_EQUAL_INT(200, g.status);
  TEST_ASSERT_EQUAL_UINT32(2, parse(g.body)["devices"].as<JsonArray>().size());

  HttpResponse put = a.call("PUT", "/api/devices",
      "{\"devices\":[{\"id\":\"mouse\",\"kind\":\"mouse\",\"enabled\":true,\"profileId\":\"razer-orochi-v2\"},"
      "{\"id\":\"keyboard\",\"kind\":\"keyboard\",\"enabled\":true,\"profileId\":\"custom\",\"name\":\"Tastiera\"}]}");
  TEST_ASSERT_EQUAL_INT(200, put.status);
  JsonDocument pd2 = parse(put.body);
  TEST_ASSERT_TRUE(pd2["restartRequired"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("razer-orochi-v2", a.registry.list()[0].profileId.c_str());

  HttpResponse bad = a.call("PUT", "/api/devices", "{\"devices\":[{\"id\":\"mouse\",\"kind\":\"mouse\",\"enabled\":true,\"mac\":\"zz\"}]}");
  TEST_ASSERT_EQUAL_INT(400, bad.status);
  JsonDocument bd = parse(bad.body);
  TEST_ASSERT_EQUAL_STRING("invalid_devices", bd["error"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("devices[0].mac", bd["field"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("razer-orochi-v2", a.registry.list()[0].profileId.c_str());  // unchanged

  TEST_ASSERT_EQUAL_INT(400, a.call("PUT", "/api/devices", "{").status);
  TEST_ASSERT_EQUAL_INT(405, a.call("DELETE", "/api/devices").status);
  TEST_ASSERT_EQUAL_INT(405, a.call("POST", "/api/profiles").status);

  a.ctx.devices = nullptr;
  TEST_ASSERT_EQUAL_INT(404, a.call("GET", "/api/devices").status);
  TEST_ASSERT_EQUAL_INT(404, a.call("GET", "/api/profiles").status);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_catalog_has_verified_logitech_default);
  RUN_TEST(test_catalog_entries_pin_verified_values);
  RUN_TEST(test_catalog_ids_unique_and_wellformed);
  RUN_TEST(test_default_devices);
  RUN_TEST(test_resolve_identity_overrides);
  RUN_TEST(test_resolve_identity_custom_defaults);
  RUN_TEST(test_resolve_mac);
  RUN_TEST(test_parse_mac);
  RUN_TEST(test_validate_devices_table);
  RUN_TEST(test_json_roundtrip);
  RUN_TEST(test_parse_devices_bad_input);
  RUN_TEST(test_registry_primary_and_flags);
  RUN_TEST(test_registry_replace_persists_and_invalid_changes_nothing);
  RUN_TEST(test_registry_corrupt_blob_falls_back_to_defaults);
  RUN_TEST(test_api_profiles_devices_routes);
  return UNITY_END();
}
