#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "api_ex.h"
#include "controller.h"
#include "fakes.h"
#include "script.h"

using namespace jig;

void setUp() {}
void tearDown() {}

namespace {

JsonDocument parse(const std::string& s) {
  JsonDocument doc;
  TEST_ASSERT_TRUE_MESSAGE(!deserializeJson(doc, s), s.c_str());
  return doc;
}

std::optional<CompileError> compile(const std::string& json, Plan& out, uint32_t seed = 1) {
  XorShiftRng rng(seed);
  return compileScript(json, rng, out);
}

std::string script(const std::string& stepsJson) { return "{\"steps\":[" + stepsJson + "]}"; }

std::string repeat(int times, const std::string& inner) {
  return "{\"op\":\"repeat\",\"times\":" + std::to_string(times) + ",\"steps\":[" + inner + "]}";
}

struct Env {
  FakeClock clock;
  XorShiftRng rng{1};
  MemStore config;
  Controller controller{clock, rng, config};
  FlakyBlobStore blobs;
  ScriptLibrary library{blobs};
  ApiContext ctx{controller};
  Env() {
    controller.begin();
    ctx.scripts = &library;
  }
  HttpResponse call(const std::string& method, const std::string& path, const std::string& body = "") {
    return handleApiEx(ctx, method, path, body);
  }
};

}  // namespace

void test_compile_ops_table() {
  Plan p;
  TEST_ASSERT_FALSE(compile(script("{\"op\":\"key\",\"key\":\"F15\"}"), p).has_value());
  TEST_ASSERT_EQUAL_UINT32(2, p.steps.size());
  TEST_ASSERT_TRUE(p.steps[0].kind == Step::Kind::KeyDown);
  TEST_ASSERT_EQUAL_HEX8(0x6A, p.steps[0].key);
  TEST_ASSERT_EQUAL_HEX8(0x00, p.steps[0].mods);
  TEST_ASSERT_EQUAL_UINT16(40, p.steps[0].waitMs);
  TEST_ASSERT_TRUE(p.steps[1].kind == Step::Kind::KeyUp);

  TEST_ASSERT_FALSE(compile(script("{\"op\":\"key\",\"key\":\"Ctrl+Alt+Delete\"}"), p).has_value());
  TEST_ASSERT_EQUAL_HEX8(0x05, p.steps[0].mods);
  TEST_ASSERT_EQUAL_HEX8(0x4C, p.steps[0].key);

  TEST_ASSERT_FALSE(compile(script("{\"op\":\"scroll\",\"notches\":3}"), p).has_value());
  TEST_ASSERT_EQUAL_UINT32(1, p.steps.size());
  TEST_ASSERT_TRUE(p.steps[0].kind == Step::Kind::Scroll);
  TEST_ASSERT_EQUAL_INT8(3, p.steps[0].notches);

  TEST_ASSERT_FALSE(compile(script("{\"op\":\"click\"}"), p).has_value());
  TEST_ASSERT_EQUAL_UINT32(1, p.steps.size());
  TEST_ASSERT_TRUE(p.steps[0].kind == Step::Kind::Click);

  TEST_ASSERT_FALSE(compile(script("{\"op\":\"wait\",\"ms\":500}"), p).has_value());
  TEST_ASSERT_EQUAL_UINT32(1, p.steps.size());
  TEST_ASSERT_TRUE(p.steps[0].kind == Step::Kind::Wait);
  TEST_ASSERT_EQUAL_UINT16(500, p.steps[0].waitMs);

  TEST_ASSERT_FALSE(compile(script("{\"op\":\"wait\",\"ms\":100000}"), p).has_value());
  uint32_t total = 0;
  for (const Step& s : p.steps) total += s.waitMs;
  TEST_ASSERT_EQUAL_UINT32(100000, total);  // split because Step::waitMs is 16 bit

  TEST_ASSERT_FALSE(compile(script(repeat(3, "{\"op\":\"click\"}")), p).has_value());
  TEST_ASSERT_EQUAL_UINT32(3, p.steps.size());
}

void test_move_ends_exactly() {
  const struct {
    int dx, dy;
  } cases[] = {{120, -45}, {2000, 2000}, {-2000, 1}, {0, 0}, {1, 0}, {0, -300}};
  for (const auto& c : cases) {
    for (uint32_t seed = 1; seed <= 100; seed++) {
      Plan p;
      const std::string json = script("{\"op\":\"move\",\"dx\":" + std::to_string(c.dx) + ",\"dy\":" + std::to_string(c.dy) + "}");
      TEST_ASSERT_FALSE(compile(json, p, seed).has_value());
      int sx = 0, sy = 0;
      for (const Step& s : p.steps) {
        TEST_ASSERT_TRUE(s.kind == Step::Kind::Move);
        sx += s.dx;
        sy += s.dy;
      }
      TEST_ASSERT_EQUAL_INT(c.dx, sx);
      TEST_ASSERT_EQUAL_INT(c.dy, sy);
    }
  }
}

void test_type_emits_shift_for_uppercase() {
  Plan p;
  TEST_ASSERT_FALSE(compile(script("{\"op\":\"type\",\"text\":\"Hi\"}"), p).has_value());
  TEST_ASSERT_EQUAL_UINT32(4, p.steps.size());  // down/up per character
  TEST_ASSERT_TRUE(p.steps[0].kind == Step::Kind::KeyDown);
  TEST_ASSERT_EQUAL_HEX8(0x02, p.steps[0].mods);
  TEST_ASSERT_EQUAL_HEX8(0x0B, p.steps[0].key);
  TEST_ASSERT_TRUE(p.steps[1].kind == Step::Kind::KeyUp);
  TEST_ASSERT_EQUAL_HEX8(0x00, p.steps[2].mods);
  TEST_ASSERT_EQUAL_HEX8(0x0C, p.steps[2].key);
  for (const Step& s : p.steps) TEST_ASSERT_TRUE(s.waitMs >= 20 && s.waitMs <= 80);
  // doubled letters still get a release between them
  TEST_ASSERT_FALSE(compile(script("{\"op\":\"type\",\"text\":\"ll\"}"), p).has_value());
  TEST_ASSERT_TRUE(p.steps[1].kind == Step::Kind::KeyUp);
}

void test_type_rejects_non_ascii() {  // RF3
  for (const char* text : {"é", "😀", "a\\u0001b", "", "tab\\u007f"}) {
    Plan p;
    p.steps.push_back({Step::Kind::Click, 0, 0, 0, 1});  // must stay untouched on error
    auto err = compile(script(std::string("{\"op\":\"type\",\"text\":\"") + text + "\"}"), p);
    TEST_ASSERT_TRUE_MESSAGE(err.has_value(), text);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("/steps/0/text", err->path.c_str(), text);
    TEST_ASSERT_EQUAL_UINT32(1, p.steps.size());
  }
  Plan p;
  TEST_ASSERT_TRUE(compile(script("{\"op\":\"type\",\"text\":\"" + std::string(201, 'a') + "\"}"), p).has_value());
  TEST_ASSERT_FALSE(compile(script("{\"op\":\"type\",\"text\":\"" + std::string(200, 'a') + "\"}"), p).has_value());
}

void test_script_bombs_rejected() {  // RF2
  const std::string click = "{\"op\":\"click\"}";
  const std::string deep4 = repeat(1, repeat(1, repeat(1, repeat(1, click))));
  const std::string deep3 = repeat(1, repeat(1, repeat(1, click)));
  std::string manyClicks;
  for (int i = 0; i < 400; i++) manyClicks += click + ",";
  manyClicks += click;
  const struct {
    const char* what;
    std::string json;
    bool ok;
  } cases[] = {
      {"nesting 4 deep", script(deep4), false},
      {"nesting 3 deep", script(deep3), true},
      {"repeat 1e9", script(repeat(1000000000, click)), false},
      {"repeat 0", script(repeat(0, click)), false},
      {"repeat 1001", script(repeat(1001, click)), false},
      {"8000 steps via nesting", script(repeat(20, repeat(20, repeat(20, click)))), false},
      {"exactly 2000 steps", script(repeat(1000, click) + "," + repeat(1000, click)), true},
      {"2001 steps", script(repeat(1000, click) + "," + repeat(1000, click) + "," + click), false},
      {"wait total over 600000", script("{\"op\":\"wait\",\"ms\":400000},{\"op\":\"wait\",\"ms\":400000}"), false},
      {"wait max total", script("{\"op\":\"wait\",\"ms\":600000}"), true},
      {"random wait counted at its maximum", script("{\"op\":\"wait\",\"minMs\":0,\"maxMs\":400000},{\"op\":\"wait\",\"minMs\":0,\"maxMs\":400000}"), false},
      {"wait min > max", script("{\"op\":\"wait\",\"minMs\":900,\"maxMs\":200}"), false},
      {"dx 1e9", script("{\"op\":\"move\",\"dx\":1000000000,\"dy\":0}"), false},
      {"dx 2001", script("{\"op\":\"move\",\"dx\":2001,\"dy\":0}"), false},
      {"dy -2001", script("{\"op\":\"move\",\"dx\":0,\"dy\":-2001}"), false},
      {"dx 2000", script("{\"op\":\"move\",\"dx\":2000,\"dy\":-2000}"), true},
      {"negative ms", script("{\"op\":\"wait\",\"ms\":-5}"), false},
      {"fractional ms", script("{\"op\":\"wait\",\"ms\":1.5}"), false},
      {"string ms", script("{\"op\":\"wait\",\"ms\":\"x\"}"), false},
      {"scroll 0", script("{\"op\":\"scroll\",\"notches\":0}"), false},
      {"scroll 1000", script("{\"op\":\"scroll\",\"notches\":1000}"), false},
      {"unknown op", script("{\"op\":\"format\"}"), false},
      {"missing op", script("{\"ms\":5}"), false},
      {"unknown key name", script("{\"op\":\"key\",\"key\":\"Hyper\"}"), false},
      {"step not an object", script("5"), false},
      {"unknown field in step", script("{\"op\":\"click\",\"dx\":1}"), false},
      {"steps not an array", "{\"steps\":5}", false},
      {"top level not an object", "[]", false},
      {"not json", "{", false},
      {"empty body", "", false},
      {"empty steps", "{\"steps\":[]}", false},
      {"unknown top-level field", "{\"steps\":[{\"op\":\"click\"}],\"x\":1}", false},
      {"over 3000 bytes", script(manyClicks), false},
  };
  for (const auto& c : cases) {
    Plan p;
    p.steps.push_back({Step::Kind::Click, 0, 0, 0, 1});
    auto err = compile(c.json, p);
    TEST_ASSERT_EQUAL_INT_MESSAGE(c.ok ? 0 : 1, err.has_value() ? 1 : 0, c.what);
    if (!c.ok) TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, p.steps.size(), c.what);  // untouched on error
    if (c.ok) TEST_ASSERT_TRUE_MESSAGE(p.steps.size() >= 1, c.what);
  }
}

void test_compile_iteration_bomb_rejected() {  // final review, Critical
  const std::string nop = "{\"op\":\"wait\",\"ms\":0}";
  const std::string move0 = "{\"op\":\"move\",\"dx\":0,\"dy\":0}";
  for (const std::string& op : {nop, move0}) {
    Plan p;
    p.steps.push_back({Step::Kind::Click, 0, 0, 0, 1});
    // 100*100*100 = 1e6 iterations that emit no steps: must be refused, not ground through
    auto err = compile(script(repeat(100, repeat(100, repeat(100, op)))), p);
    TEST_ASSERT_TRUE_MESSAGE(err.has_value(), op.c_str());
    TEST_ASSERT_EQUAL_UINT32(1, p.steps.size());
  }
  Plan heavy;
  // 1000*1000*1000 = 1e9 iterations: the reviewer measured ~7 minutes on a laptop before the fix
  TEST_ASSERT_TRUE(compile(script(repeat(1000, repeat(1000, repeat(1000, nop)))), heavy).has_value());
  TEST_ASSERT_TRUE(heavy.steps.empty());
  Plan ok;
  TEST_ASSERT_FALSE(compile(script(repeat(100, nop)), ok).has_value());  // a modest number of no-op steps is fine
}

void test_library_storage_failure_reported() {  // final review, Critical
  FlakyBlobStore blobs;
  ScriptLibrary lib(blobs);
  const std::string s = script("{\"op\":\"click\"}");
  TEST_ASSERT_TRUE(lib.put("ok", s).ok);
  blobs.failPuts = true;
  CmdResult r = lib.put("full", s);
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("storage_error", r.error.c_str());
  TEST_ASSERT_FALSE(lib.get("full").has_value());
  TEST_ASSERT_EQUAL_UINT32(2, lib.names().size());  // built-in + "ok" only
  blobs.failPuts = false;
  TEST_ASSERT_TRUE(lib.put("full", s).ok);
}

void test_script_size_limit_fits_nvs() {
  MemBlobStore blobs;
  ScriptLibrary lib(blobs);
  std::string padded = "{\"steps\":[{\"op\":\"click\"}]," + std::string(kMaxScriptBytes, ' ') + "}";
  TEST_ASSERT_FALSE(lib.put("big", padded).ok);
  TEST_ASSERT_TRUE(kMaxScriptBytes <= 1536);  // 8 scripts must fit the 20 KB NVS partition next to WiFi credentials
}

void test_wait_random_in_range() {
  bool differs = false;
  uint16_t first = 0;
  for (uint32_t seed = 1; seed <= 200; seed++) {
    Plan p;
    TEST_ASSERT_FALSE(compile(script("{\"op\":\"wait\",\"minMs\":200,\"maxMs\":900}"), p, seed).has_value());
    TEST_ASSERT_EQUAL_UINT32(1, p.steps.size());
    TEST_ASSERT_TRUE(p.steps[0].waitMs >= 200 && p.steps[0].waitMs <= 900);
    if (seed == 1) first = p.steps[0].waitMs;
    else if (p.steps[0].waitMs != first) differs = true;
  }
  TEST_ASSERT_TRUE(differs);
}

void test_library_put_get_remove() {
  MemBlobStore blobs;
  ScriptLibrary lib(blobs);
  auto names = lib.names();
  TEST_ASSERT_EQUAL_UINT32(1, names.size());
  TEST_ASSERT_EQUAL_STRING("keepalive-key", names[0].c_str());
  TEST_ASSERT_TRUE(lib.isBuiltin("keepalive-key"));

  const std::string s = script("{\"op\":\"click\"}");
  CmdResult r = lib.put("demo", s);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING(s.c_str(), lib.get("demo")->c_str());
  TEST_ASSERT_FALSE(lib.isBuiltin("demo"));
  names = lib.names();
  TEST_ASSERT_EQUAL_UINT32(2, names.size());
  TEST_ASSERT_EQUAL_STRING("demo", names[1].c_str());
  TEST_ASSERT_TRUE(lib.remove("demo").ok);
  TEST_ASSERT_FALSE(lib.get("demo").has_value());
  CmdResult again = lib.remove("demo");
  TEST_ASSERT_FALSE(again.ok);
  TEST_ASSERT_EQUAL_STRING("not_found", again.error.c_str());
  // the built-in script compiles to a single F15 tap
  Plan p;
  XorShiftRng rng(1);
  TEST_ASSERT_FALSE(compileScript(*lib.get("keepalive-key"), rng, p).has_value());
  TEST_ASSERT_EQUAL_HEX8(0x6A, p.steps[0].key);
}

void test_library_limits() {
  MemBlobStore blobs;
  ScriptLibrary lib(blobs);
  const std::string s = script("{\"op\":\"click\"}");
  for (int i = 0; i < 8; i++) TEST_ASSERT_TRUE(lib.put("s" + std::to_string(i), s).ok);
  CmdResult over = lib.put("ninth", s);
  TEST_ASSERT_FALSE(over.ok);
  TEST_ASSERT_EQUAL_STRING("limit_reached", over.error.c_str());
  TEST_ASSERT_TRUE(lib.put("s3", script("{\"op\":\"scroll\",\"notches\":1}")).ok);  // overwriting is fine at the limit

  for (const char* bad : {"", "UPPER", "../x", "has space", "a/b"}) {
    CmdResult r = lib.put(bad, s);
    TEST_ASSERT_FALSE_MESSAGE(r.ok, bad);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("invalid_script", r.error.c_str(), bad);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("name", r.field.c_str(), bad);
  }
  TEST_ASSERT_FALSE(lib.put(std::string(25, 'a'), s).ok);
  CmdResult ro = lib.put("keepalive-key", s);
  TEST_ASSERT_EQUAL_STRING("read_only", ro.error.c_str());
  TEST_ASSERT_EQUAL_STRING("read_only", lib.remove("keepalive-key").error.c_str());
}

void test_library_invalid_script_not_stored() {  // RF2
  MemBlobStore blobs;
  ScriptLibrary lib(blobs);
  for (const char* bad : {"{", "[]", "{\"steps\":[{\"op\":\"nope\"}]}"}) {
    CmdResult r = lib.put("bad", bad);
    TEST_ASSERT_FALSE_MESSAGE(r.ok, bad);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("invalid_script", r.error.c_str(), bad);
  }
  CmdResult big = lib.put("bad", "{\"steps\":[" + std::string(3100, ' ') + "]}");
  TEST_ASSERT_FALSE(big.ok);
  TEST_ASSERT_FALSE(lib.get("bad").has_value());
  TEST_ASSERT_TRUE(blobs.data.empty());
}

void test_script_routes() {
  Env e;
  const std::string s = script("{\"op\":\"type\",\"text\":\"ciao\"}");
  HttpResponse put = e.call("PUT", "/api/scripts/saluto", s);
  TEST_ASSERT_EQUAL_INT(200, put.status);

  HttpResponse list = e.call("GET", "/api/scripts");
  TEST_ASSERT_EQUAL_INT(200, list.status);
  JsonDocument ld = parse(list.body);
  TEST_ASSERT_EQUAL_UINT32(2, ld["scripts"].as<JsonArray>().size());
  TEST_ASSERT_EQUAL_STRING("keepalive-key", ld["scripts"][0]["name"].as<const char*>());
  TEST_ASSERT_TRUE(ld["scripts"][0]["builtin"].as<bool>());
  TEST_ASSERT_FALSE(ld["scripts"][1]["builtin"].as<bool>());

  HttpResponse one = e.call("GET", "/api/scripts/saluto");
  TEST_ASSERT_EQUAL_INT(200, one.status);
  JsonDocument od = parse(one.body);
  TEST_ASSERT_EQUAL_STRING("saluto", od["name"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("ciao", od["script"]["steps"][0]["text"].as<const char*>());

  HttpResponse bad = e.call("PUT", "/api/scripts/rotto", script("{\"op\":\"move\",\"dx\":9999,\"dy\":0}"));
  TEST_ASSERT_EQUAL_INT(400, bad.status);
  JsonDocument bd = parse(bad.body);
  TEST_ASSERT_EQUAL_STRING("invalid_script", bd["error"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("/steps/0/dx", bd["field"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(404, e.call("GET", "/api/scripts/rotto").status);

  TEST_ASSERT_EQUAL_INT(413, e.call("PUT", "/api/scripts/grande", std::string(kMaxBodyBytes + 1, ' ')).status);
  TEST_ASSERT_EQUAL_INT(400, e.call("PUT", "/api/scripts/BAD", s).status);
  TEST_ASSERT_EQUAL_INT(404, e.call("GET", "/api/scripts/nessuno").status);

  TEST_ASSERT_EQUAL_INT(403, e.call("DELETE", "/api/scripts/keepalive-key").status);
  TEST_ASSERT_EQUAL_INT(403, e.call("PUT", "/api/scripts/keepalive-key", s).status);
  TEST_ASSERT_EQUAL_INT(200, e.call("DELETE", "/api/scripts/saluto").status);
  TEST_ASSERT_EQUAL_INT(404, e.call("DELETE", "/api/scripts/saluto").status);
  TEST_ASSERT_EQUAL_INT(405, e.call("POST", "/api/scripts").status);
  TEST_ASSERT_EQUAL_INT(405, e.call("PATCH", "/api/scripts/saluto").status);

  for (int i = 0; i < 8; i++) TEST_ASSERT_EQUAL_INT(200, e.call("PUT", "/api/scripts/s" + std::to_string(i), s).status);
  TEST_ASSERT_EQUAL_INT(409, e.call("PUT", "/api/scripts/extra", s).status);
  e.blobs.failPuts = true;
  TEST_ASSERT_EQUAL_INT(507, e.call("PUT", "/api/scripts/s0", script("{\"op\":\"scroll\",\"notches\":2}")).status);
  e.blobs.failPuts = false;

  e.ctx.scripts = nullptr;
  TEST_ASSERT_EQUAL_INT(404, e.call("GET", "/api/scripts").status);
  TEST_ASSERT_EQUAL_INT(404, e.call("GET", "/api/scripts/x").status);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_compile_ops_table);
  RUN_TEST(test_move_ends_exactly);
  RUN_TEST(test_type_emits_shift_for_uppercase);
  RUN_TEST(test_type_rejects_non_ascii);
  RUN_TEST(test_script_bombs_rejected);
  RUN_TEST(test_compile_iteration_bomb_rejected);
  RUN_TEST(test_library_storage_failure_reported);
  RUN_TEST(test_script_size_limit_fits_nvs);
  RUN_TEST(test_wait_random_in_range);
  RUN_TEST(test_library_put_get_remove);
  RUN_TEST(test_library_limits);
  RUN_TEST(test_library_invalid_script_not_stored);
  RUN_TEST(test_script_routes);
  return UNITY_END();
}
