#include <unity.h>

#include <ArduinoJson.h>

#include <set>
#include <string>

#include "api_ex.h"
#include "appearance.h"
#include "controller.h"
#include "fakes.h"

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

Appearance withProfile(const char* profile) {
  Appearance a;
  a.profile = profile;
  return a;
}

struct Api {
  FakeClock clock;
  XorShiftRng rng{1};
  MemStore config;
  Controller controller{clock, rng, config};
  FlakyBlobStore blobs;
  AppearanceStore store{blobs};
  ApiContext ctx{controller};

  Api() {
    controller.begin();
    store.begin();
    ctx.appearance = &store;
  }
  HttpResponse call(const std::string& method, const std::string& path, const std::string& body = "") {
    return handleApiEx(ctx, method, path, body);
  }
};

}  // namespace

void test_the_default_is_custom_not_a_brand() {
  Appearance a;
  TEST_ASSERT_EQUAL_STRING("custom", a.profile.c_str());
  TEST_ASSERT_FALSE(validateAppearance(a).has_value());
  Identity id = resolveIdentity(a);
  TEST_ASSERT_EQUAL_STRING("Bluetooth Mouse", id.name.c_str());
  TEST_ASSERT_EQUAL_STRING("Generic", id.manufacturer.c_str());
  TEST_ASSERT_EQUAL_HEX16(0x1209, id.vid);
  TEST_ASSERT_EQUAL_HEX16(0x0001, id.pid);
  TEST_ASSERT_FALSE(id.oui.has_value());                       // the chip's own address: no borrowed vendor prefix
  TEST_ASSERT_EQUAL_HEX16(0x03C2, id.appearance);              // a mouse
  MemBlobStore empty;
  AppearanceStore fresh(empty);
  fresh.begin();
  TEST_ASSERT_EQUAL_STRING("custom", fresh.get().profile.c_str());
}

void test_catalog_is_mouse_only_and_pins_verified_values() {
  const struct {
    const char* id;
    uint16_t vid, pid;
    uint8_t oui0, oui1, oui2;
  } expected[] = {
      {"logitech-mx-master-3s", 0x046D, 0xB034, 0x00, 0x1F, 0x20},
      {"razer-orochi-v2", 0x1532, 0x0095, 0x44, 0x5E, 0xCD},
      {"apple-magic-mouse-2", 0x004C, 0x0269, 0x58, 0xAD, 0x12},
      // Linux drivers/hid/hid-logitech-hidpp.c, "... mouse over Bluetooth" (vendor 046D)
      {"logitech-mx-master-3", 0x046D, 0xB023, 0x00, 0x1F, 0x20},
      {"logitech-mx-master-2s", 0x046D, 0xB019, 0x00, 0x1F, 0x20},
      {"logitech-mx-master-4", 0x046D, 0xB042, 0x00, 0x1F, 0x20},
      {"logitech-mx-anywhere-3", 0x046D, 0xB025, 0x00, 0x1F, 0x20},
      {"logitech-mx-anywhere-3s", 0x046D, 0xB037, 0x00, 0x1F, 0x20},
      {"logitech-m720-triathlon", 0x046D, 0xB015, 0x00, 0x1F, 0x20},
      {"logitech-mx-vertical", 0x046D, 0xB020, 0x00, 0x1F, 0x20},
      // Linux drivers/hid/hid-ids.h: BT_VENDOR_ID_APPLE 0x004c with MAGICMOUSE 0x030d / MAGICMOUSE2_USBC 0x0323
      {"apple-magic-mouse", 0x004C, 0x030D, 0x58, 0xAD, 0x12},
      {"apple-magic-mouse-2-usbc", 0x004C, 0x0323, 0x58, 0xAD, 0x12},
  };
  for (const auto& e : expected) {
    const Profile* p = findProfile(e.id);
    TEST_ASSERT_NOT_NULL_MESSAGE(p, e.id);
    TEST_ASSERT_TRUE_MESSAGE(p->kind == DeviceKind::Mouse, e.id);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(e.vid, p->identity.vid, e.id);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(e.pid, p->identity.pid, e.id);
    TEST_ASSERT_TRUE_MESSAGE(p->identity.oui.has_value(), e.id);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(e.oui0, (*p->identity.oui)[0], e.id);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(e.oui1, (*p->identity.oui)[1], e.id);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(e.oui2, (*p->identity.oui)[2], e.id);
  }
  std::set<std::string> seen;
  for (const Profile& p : catalog()) {
    TEST_ASSERT_TRUE_MESSAGE(p.kind == DeviceKind::Mouse, p.id.c_str());
    TEST_ASSERT_TRUE_MESSAGE(seen.insert(p.id).second, p.id.c_str());
    TEST_ASSERT_TRUE_MESSAGE(p.id != "custom", "custom is reserved");
    TEST_ASSERT_TRUE_MESSAGE(!p.identity.name.empty() && p.identity.name.size() <= 29, p.id.c_str());
    if (p.identity.oui) TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, (*p.identity.oui)[0] & 1, p.id.c_str());  // unicast
  }
  TEST_ASSERT_TRUE(catalog().size() >= 12);
}

void test_overrides_win_over_the_profile() {
  Appearance a = withProfile("logitech-mx-master-3s");
  a.name = "Foo";
  a.vid = 0x1234;
  Identity id = resolveIdentity(a);
  TEST_ASSERT_EQUAL_STRING("Foo", id.name.c_str());
  TEST_ASSERT_EQUAL_STRING("Logitech", id.manufacturer.c_str());
  TEST_ASSERT_EQUAL_HEX16(0x1234, id.vid);
  TEST_ASSERT_EQUAL_HEX16(0xB034, id.pid);
  Appearance c;  // custom with only a name
  c.name = "Desk Mouse";
  TEST_ASSERT_EQUAL_STRING("Desk Mouse", resolveIdentity(c).name.c_str());
  TEST_ASSERT_EQUAL_STRING("Generic", resolveIdentity(c).manufacturer.c_str());
}

void test_the_bluetooth_address() {
  const uint8_t logi[6] = {0x00, 0x1F, 0x20, 0xAA, 0xBB, 0xCC};
  Appearance a = withProfile("logitech-mx-master-3s");
  auto mac = resolveMac(a, resolveIdentity(a), kEfuse);
  TEST_ASSERT_TRUE(mac.has_value());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(logi, mac->data(), 6);  // the model's prefix + the chip's own last 3 bytes

  a.mac = "02:11:22:33:44:55";
  const uint8_t over[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(over, resolveMac(a, resolveIdentity(a), kEfuse)->data(), 6);

  Appearance custom;
  TEST_ASSERT_FALSE(resolveMac(custom, resolveIdentity(custom), kEfuse).has_value());  // keep the chip's own
}

void test_parse_mac() {
  TEST_ASSERT_TRUE(parseMac("00:1f:20:AA:bb:0C").has_value());
  for (const char* bad : {"", "00:1F:20:AA:BB", "00:1F:20:AA:BB:CC:DD", "zz:1F:20:AA:BB:CC", "001F20AABBCC", "00-1F-20-AA-BB-CC"})
    TEST_ASSERT_FALSE_MESSAGE(parseMac(bad).has_value(), bad);
  TEST_ASSERT_TRUE(parseMac("01:1F:20:AA:BB:CC").has_value());  // multicast parses; validate rejects it
}

void test_validation() {
  struct Case {
    const char* what;
    Appearance a;
    const char* field;
  };
  std::vector<Case> cases;
  { Appearance a = withProfile("nope"); cases.push_back({"unknown profile", a, "profile"}); }
  { Appearance a; a.name = ""; cases.push_back({"empty name", a, "name"}); }
  { Appearance a; a.name = std::string(30, 'n'); cases.push_back({"name too long", a, "name"}); }
  { Appearance a; a.name = "bad\nname"; cases.push_back({"control character", a, "name"}); }
  { Appearance a; a.manufacturer = std::string(33, 'm'); cases.push_back({"manufacturer too long", a, "manufacturer"}); }
  { Appearance a; a.mac = "00:1F:20:AA:BB"; cases.push_back({"5-octet mac", a, "mac"}); }
  { Appearance a; a.mac = "01:1F:20:AA:BB:CC"; cases.push_back({"multicast mac", a, "mac"}); }
  for (const Case& c : cases) {
    auto err = validateAppearance(c.a);
    TEST_ASSERT_TRUE_MESSAGE(err.has_value(), c.what);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.field, err->field.c_str(), c.what);
  }
  Appearance ok = withProfile("razer-orochi-v2");
  ok.name = std::string(29, 'n');
  ok.mac = "02:11:22:33:44:55";
  TEST_ASSERT_FALSE(validateAppearance(ok).has_value());
}

void test_json_round_trip_and_what_the_computer_sees() {
  Appearance a = withProfile("logitech-mx-master-3s");
  a.name = "Desk";
  a.vid = 0x1234;
  JsonDocument d = parse(appearanceToJson(a));
  TEST_ASSERT_EQUAL_STRING("logitech-mx-master-3s", d["profile"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Desk", d["name"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Desk", d["shownAs"].as<const char*>());
  TEST_ASSERT_TRUE(d["profiles"].size() >= 12);                    // what the dashboard offers
  TEST_ASSERT_EQUAL_STRING("Bluetooth Mouse", d["custom"]["name"].as<const char*>());
  TEST_ASSERT_TRUE(d["restartRequired"].isNull());
  TEST_ASSERT_TRUE(parse(appearanceToJson(a, true))["restartRequired"].as<bool>());
  TEST_ASSERT_TRUE(parse(appearanceToJson(Appearance{}))["name"].isNull());
  TEST_ASSERT_EQUAL_STRING("Bluetooth Mouse", parse(appearanceToJson(Appearance{}))["shownAs"].as<const char*>());

  Appearance back;
  ParseError err;
  TEST_ASSERT_TRUE_MESSAGE(parseAppearance(appearanceToJson(a), back, err), err.message.c_str());  // a GET body can be sent back
  TEST_ASSERT_EQUAL_STRING("logitech-mx-master-3s", back.profile.c_str());
  TEST_ASSERT_EQUAL_STRING("Desk", back.name->c_str());
  TEST_ASSERT_EQUAL_HEX16(0x1234, *back.vid);
  TEST_ASSERT_FALSE(back.pid.has_value());
}

void test_parse_rejects_bad_input_with_a_field() {
  struct Case { const char* body; const char* field; };
  const Case cases[] = {
      {"", ""}, {"{", ""}, {"[]", ""},
      {"{\"profile\":5}", "profile"},
      {"{\"name\":5}", "name"},
      {"{\"vid\":-1}", "vid"}, {"{\"pid\":70000}", "pid"}, {"{\"vid\":\"x\"}", "vid"},
      {"{\"mac\":5}", "mac"},
      {"{\"bogus\":1}", "bogus"},
      {"{\"devices\":[]}", "devices"},
  };
  for (const Case& c : cases) {
    Appearance a;
    ParseError err;
    TEST_ASSERT_FALSE_MESSAGE(parseAppearance(c.body, a, err), c.body);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.field, err.field.c_str(), c.body);
  }
  Appearance a;
  ParseError err;
  TEST_ASSERT_TRUE(parseAppearance("{}", a, err));            // a full replacement: omitted means default
  TEST_ASSERT_EQUAL_STRING("custom", a.profile.c_str());
}

void test_store_persists_and_survives_a_corrupt_blob() {
  MemBlobStore blobs;
  {
    AppearanceStore s(blobs);
    s.begin();
    Appearance a = withProfile("apple-magic-mouse-2");
    TEST_ASSERT_TRUE(s.set(a).ok);
  }
  AppearanceStore again(blobs);
  again.begin();
  TEST_ASSERT_EQUAL_STRING("apple-magic-mouse-2", again.get().profile.c_str());
  const uint8_t efuse[6] = {1, 2, 3, 4, 5, 6};
  TEST_ASSERT_EQUAL_STRING("Magic Mouse 2", again.resolved(efuse).identity.name.c_str());

  blobs.put("appearance", "{not json");
  AppearanceStore broken(blobs);
  broken.begin();
  TEST_ASSERT_EQUAL_STRING("custom", broken.get().profile.c_str());
  blobs.put("appearance", "{\"profile\":\"nope\"}");        // parses but invalid
  AppearanceStore invalid(blobs);
  invalid.begin();
  TEST_ASSERT_EQUAL_STRING("custom", invalid.get().profile.c_str());
}

void test_an_invalid_change_changes_nothing_and_a_full_storage_is_reported() {
  FlakyBlobStore blobs;
  AppearanceStore s(blobs);
  s.begin();
  TEST_ASSERT_TRUE(s.set(withProfile("razer-orochi-v2")).ok);
  CmdResult bad = s.set(withProfile("nope"));
  TEST_ASSERT_EQUAL_STRING("invalid_appearance", bad.error.c_str());
  TEST_ASSERT_EQUAL_STRING("profile", bad.field.c_str());
  TEST_ASSERT_EQUAL_STRING("razer-orochi-v2", s.get().profile.c_str());
  blobs.failPuts = true;
  CmdResult full = s.set(withProfile("apple-magic-mouse-2"));
  TEST_ASSERT_EQUAL_STRING("storage_error", full.error.c_str());
  TEST_ASSERT_EQUAL_STRING("razer-orochi-v2", s.get().profile.c_str());
}

void test_the_routes() {
  Api a;
  HttpResponse g = a.call("GET", "/api/appearance");
  TEST_ASSERT_EQUAL_INT(200, g.status);
  JsonDocument gd = parse(g.body);
  TEST_ASSERT_EQUAL_STRING("custom", gd["profile"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Bluetooth Mouse", gd["shownAs"].as<const char*>());
  TEST_ASSERT_TRUE(gd["profiles"].size() >= 12);

  HttpResponse put = a.call("PUT", "/api/appearance", "{\"profile\":\"razer-orochi-v2\"}");
  TEST_ASSERT_EQUAL_INT(200, put.status);
  TEST_ASSERT_TRUE(parse(put.body)["restartRequired"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("razer-orochi-v2", a.store.get().profile.c_str());

  HttpResponse named = a.call("PUT", "/api/appearance", "{\"profile\":\"custom\",\"name\":\"Desk Mouse\"}");
  TEST_ASSERT_EQUAL_INT(200, named.status);
  TEST_ASSERT_EQUAL_STRING("Desk Mouse", parse(named.body)["shownAs"].as<const char*>());

  HttpResponse bad = a.call("PUT", "/api/appearance", "{\"mac\":\"zz\"}");
  TEST_ASSERT_EQUAL_INT(400, bad.status);
  TEST_ASSERT_EQUAL_STRING("invalid_appearance", parse(bad.body)["error"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("mac", parse(bad.body)["field"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Desk Mouse", a.store.get().name->c_str());  // unchanged

  TEST_ASSERT_EQUAL_INT(400, a.call("PUT", "/api/appearance", "{").status);
  TEST_ASSERT_EQUAL_INT(405, a.call("DELETE", "/api/appearance").status);
  TEST_ASSERT_EQUAL_INT(413, a.call("PUT", "/api/appearance", std::string(kMaxSmallBodyBytes + 1, ' ')).status);
  a.blobs.failPuts = true;
  TEST_ASSERT_EQUAL_INT(507, a.call("PUT", "/api/appearance", "{\"profile\":\"custom\"}").status);
  a.blobs.failPuts = false;

  // the old "list of devices" routes are gone
  TEST_ASSERT_EQUAL_INT(404, a.call("GET", "/api/devices").status);
  TEST_ASSERT_EQUAL_INT(404, a.call("GET", "/api/profiles").status);
  a.ctx.appearance = nullptr;
  TEST_ASSERT_EQUAL_INT(404, a.call("GET", "/api/appearance").status);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_default_is_custom_not_a_brand);
  RUN_TEST(test_catalog_is_mouse_only_and_pins_verified_values);
  RUN_TEST(test_overrides_win_over_the_profile);
  RUN_TEST(test_the_bluetooth_address);
  RUN_TEST(test_parse_mac);
  RUN_TEST(test_validation);
  RUN_TEST(test_json_round_trip_and_what_the_computer_sees);
  RUN_TEST(test_parse_rejects_bad_input_with_a_field);
  RUN_TEST(test_store_persists_and_survives_a_corrupt_blob);
  RUN_TEST(test_an_invalid_change_changes_nothing_and_a_full_storage_is_reported);
  RUN_TEST(test_the_routes);
  return UNITY_END();
}
