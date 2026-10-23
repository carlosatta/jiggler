#include "script.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <initializer_list>

#include "keymap.h"

namespace jig {

namespace {

constexpr int kMaxNesting = 3;
constexpr int kMaxCoordinate = 2000;
constexpr int kMaxRepeat = 1000;
constexpr int kMaxScrollNotches = 100;
constexpr int64_t kMaxWaitMs = 600000;
constexpr size_t kMaxTextChars = 200;
constexpr uint16_t kKeyHoldMs = 40;
constexpr uint16_t kKeyReleaseMs = 20;
constexpr uint16_t kClickWaitMs = 100;
constexpr uint16_t kScrollWaitMs = 100;
constexpr uint16_t kMaxStepWaitMs = 60000;
constexpr const char* kBuiltinName = "keepalive-key";
constexpr const char* kBuiltinJson = "{\"steps\":[{\"op\":\"key\",\"key\":\"F15\"}]}";
constexpr const char* kBlobPrefix = "script/";

struct ValidationRng : Rng {
  uint32_t s = 2463534242u;
  uint32_t next() override {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
};

struct Ctx {
  Rng& rng;
  std::vector<Step> steps;
  uint64_t worstWaitMs = 0;  // random waits counted at their maximum
  size_t ops = 0;            // operations visited, including ones that emit no step (wait 0, move 0,0)
};

using Err = std::optional<CompileError>;

Err fail(const std::string& path, const std::string& message) { return CompileError{path, message}; }

bool readInt(JsonVariantConst v, int64_t lo, int64_t hi, int64_t& out) {
  if (!v.is<int64_t>()) return false;
  out = v.as<int64_t>();
  return out >= lo && out <= hi;
}

bool onlyKeys(JsonObjectConst o, std::initializer_list<const char*> allowed, std::string& bad) {
  for (JsonPairConst kv : o) {
    bool ok = false;
    for (const char* a : allowed) ok |= std::string(kv.key().c_str()) == a;
    if (!ok) {
      bad = kv.key().c_str();
      return false;
    }
  }
  return true;
}

Err limits(Ctx& ctx, const std::string& path) {
  if (ctx.steps.size() > kMaxScriptSteps) return fail(path, "script is too long (max 2000 steps)");
  if (ctx.worstWaitMs > static_cast<uint64_t>(kMaxWaitMs)) return fail(path, "total waiting time exceeds 600000 ms");
  return std::nullopt;
}

void push(Ctx& ctx, Step step, uint64_t worstWait) {
  ctx.steps.push_back(step);
  ctx.worstWaitMs += worstWait;
}

void pushWait(Ctx& ctx, int64_t chosenMs, int64_t worstMs) {
  int64_t remaining = chosenMs;
  while (remaining > 0) {
    const uint16_t chunk = static_cast<uint16_t>(std::min<int64_t>(remaining, kMaxStepWaitMs));
    ctx.steps.push_back({Step::Kind::Wait, 0, 0, 0, chunk});
    remaining -= chunk;
  }
  ctx.worstWaitMs += static_cast<uint64_t>(worstMs);
}

Err compileSteps(JsonArrayConst steps, int depth, const std::string& path, Ctx& ctx);

Err compileOp(JsonObjectConst o, int depth, const std::string& path, Ctx& ctx) {
  if (++ctx.ops > kMaxCompileOps) return fail(path, "script is too complex (max 10000 operations)");
  if (!o["op"].is<const char*>()) return fail(path + "/op", "is required and must be a string");
  const std::string op = o["op"].as<std::string>();
  std::string bad;
  int64_t n = 0;

  if (op == "move") {
    if (!onlyKeys(o, {"op", "dx", "dy"}, bad)) return fail(path + "/" + bad, "unknown field");
    int64_t dx = 0, dy = 0;
    if (!readInt(o["dx"], -kMaxCoordinate, kMaxCoordinate, dx)) return fail(path + "/dx", "must be an integer from -2000 to 2000");
    if (!readInt(o["dy"], -kMaxCoordinate, kMaxCoordinate, dy)) return fail(path + "/dy", "must be an integer from -2000 to 2000");
    const size_t before = ctx.steps.size();
    appendHumanMove(ctx.steps, static_cast<int>(dx), static_cast<int>(dy), ctx.rng);
    for (size_t i = before; i < ctx.steps.size(); i++) ctx.worstWaitMs += ctx.steps[i].waitMs;
  } else if (op == "click") {
    if (!onlyKeys(o, {"op"}, bad)) return fail(path + "/" + bad, "unknown field");
    push(ctx, {Step::Kind::Click, 0, 0, 0, kClickWaitMs}, kClickWaitMs);
  } else if (op == "scroll") {
    if (!onlyKeys(o, {"op", "notches"}, bad)) return fail(path + "/" + bad, "unknown field");
    if (!readInt(o["notches"], -kMaxScrollNotches, kMaxScrollNotches, n) || n == 0)
      return fail(path + "/notches", "must be a non-zero integer from -100 to 100");
    push(ctx, {Step::Kind::Scroll, 0, 0, static_cast<int8_t>(n), kScrollWaitMs}, kScrollWaitMs);
  } else if (op == "key") {
    if (!onlyKeys(o, {"op", "key"}, bad)) return fail(path + "/" + bad, "unknown field");
    auto chord = o["key"].is<const char*>() ? keyFromName(o["key"].as<std::string>()) : std::nullopt;
    if (!chord) return fail(path + "/key", "unknown key name");
    push(ctx, {Step::Kind::KeyDown, 0, 0, 0, kKeyHoldMs, chord->usage, chord->mods}, kKeyHoldMs);
    push(ctx, {Step::Kind::KeyUp, 0, 0, 0, kKeyReleaseMs}, kKeyReleaseMs);
  } else if (op == "type") {
    if (!onlyKeys(o, {"op", "text"}, bad)) return fail(path + "/" + bad, "unknown field");
    if (!o["text"].is<const char*>()) return fail(path + "/text", "must be a string");
    const std::string text = o["text"].as<std::string>();
    if (text.empty() || text.size() > kMaxTextChars) return fail(path + "/text", "must be 1 to 200 characters");
    auto chords = charsToChords(text);
    if (!chords) return fail(path + "/text", "only printable ASCII, newline and tab are supported");
    for (const KeyChord& k : *chords) {
      push(ctx, {Step::Kind::KeyDown, 0, 0, 0, static_cast<uint16_t>(randRange(ctx.rng, 25, 45)), k.usage, k.mods}, 45);
      push(ctx, {Step::Kind::KeyUp, 0, 0, 0, static_cast<uint16_t>(randRange(ctx.rng, 30, 70))}, 70);
      if (ctx.steps.size() > kMaxScriptSteps) break;
    }
  } else if (op == "wait") {
    if (o["ms"].isNull() == o["minMs"].isNull() && o["ms"].isNull() == o["maxMs"].isNull() && o["ms"].isNull())
      return fail(path + "/ms", "wait needs ms, or minMs and maxMs");
    if (!o["ms"].isNull()) {
      if (!onlyKeys(o, {"op", "ms"}, bad)) return fail(path + "/" + bad, "unknown field");
      if (!readInt(o["ms"], 0, kMaxWaitMs, n)) return fail(path + "/ms", "must be an integer from 0 to 600000");
      pushWait(ctx, n, n);
    } else {
      if (!onlyKeys(o, {"op", "minMs", "maxMs"}, bad)) return fail(path + "/" + bad, "unknown field");
      int64_t lo = 0, hi = 0;
      if (!readInt(o["minMs"], 0, kMaxWaitMs, lo)) return fail(path + "/minMs", "must be an integer from 0 to 600000");
      if (!readInt(o["maxMs"], 0, kMaxWaitMs, hi)) return fail(path + "/maxMs", "must be an integer from 0 to 600000");
      if (hi < lo) return fail(path + "/maxMs", "must be >= minMs");
      pushWait(ctx, randRange(ctx.rng, static_cast<int32_t>(lo), static_cast<int32_t>(hi)), hi);
    }
  } else if (op == "repeat") {
    if (!onlyKeys(o, {"op", "times", "steps"}, bad)) return fail(path + "/" + bad, "unknown field");
    if (!readInt(o["times"], 1, kMaxRepeat, n)) return fail(path + "/times", "must be an integer from 1 to 1000");
    if (!o["steps"].is<JsonArrayConst>() || o["steps"].as<JsonArrayConst>().size() == 0)
      return fail(path + "/steps", "must be a non-empty array");
    if (depth + 1 > kMaxNesting) return fail(path, "repeat blocks can be nested at most 3 deep");
    for (int64_t i = 0; i < n; i++) {
      if (auto err = compileSteps(o["steps"].as<JsonArrayConst>(), depth + 1, path + "/steps", ctx)) return err;
    }
  } else {
    return fail(path + "/op", "unknown op");
  }
  return limits(ctx, path);
}

Err compileSteps(JsonArrayConst steps, int depth, const std::string& path, Ctx& ctx) {
  size_t i = 0;
  for (JsonVariantConst item : steps) {
    const std::string itemPath = path + "/" + std::to_string(i);
    if (!item.is<JsonObjectConst>()) return fail(itemPath, "must be an object");
    if (auto err = compileOp(item.as<JsonObjectConst>(), depth, itemPath, ctx)) return err;
    i++;
  }
  return std::nullopt;
}

bool validName(const std::string& name) {
  if (name.empty() || name.size() > 24) return false;
  for (char c : name)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  return true;
}

CmdResult failCmd(const char* error, const std::string& field, const std::string& message) {
  CmdResult r;
  r.error = error;
  r.field = field;
  r.message = message;
  return r;
}

}  // namespace

std::optional<CompileError> compileScript(const std::string& json, Rng& rng, Plan& out) {
  if (json.size() > kMaxScriptBytes) return CompileError{"", "script is too large (max 3000 bytes)"};
  JsonDocument doc;
  if (deserializeJson(doc, json, DeserializationOption::NestingLimit(24)) || !doc.is<JsonObject>())
    return CompileError{"", "must be a JSON object like {\"steps\":[...]}"};
  for (JsonPair kv : doc.as<JsonObject>())
    if (std::string(kv.key().c_str()) != "steps") return CompileError{std::string("/") + kv.key().c_str(), "unknown field"};
  if (!doc["steps"].is<JsonArray>() || doc["steps"].as<JsonArray>().size() == 0)
    return CompileError{"/steps", "must be a non-empty array"};

  Ctx ctx{rng, {}, 0};
  if (auto err = compileSteps(doc["steps"].as<JsonArrayConst>(), 0, "/steps", ctx)) return err;
  out.steps = std::move(ctx.steps);
  return std::nullopt;
}

ScriptLibrary::ScriptLibrary(BlobStore& blobs) : blobs_(blobs) {}

bool ScriptLibrary::isBuiltin(const std::string& name) const { return name == kBuiltinName; }

std::vector<std::string> ScriptLibrary::names() const {
  std::vector<std::string> user;
  const std::string prefix = kBlobPrefix;
  for (const std::string& key : blobs_.keys(prefix)) user.push_back(key.substr(prefix.size()));
  std::sort(user.begin(), user.end());
  std::vector<std::string> all{kBuiltinName};
  all.insert(all.end(), user.begin(), user.end());
  return all;
}

std::optional<std::string> ScriptLibrary::get(const std::string& name) const {
  if (isBuiltin(name)) return std::string(kBuiltinJson);
  if (!validName(name)) return std::nullopt;
  const std::string json = blobs_.get(kBlobPrefix + name);
  if (json.empty()) return std::nullopt;
  return json;
}

CmdResult ScriptLibrary::put(const std::string& name, const std::string& json) {
  if (!validName(name)) return failCmd("invalid_script", "name", "must be 1 to 24 characters of a-z 0-9 _ -");
  if (isBuiltin(name)) return failCmd("read_only", "name", "the built-in script cannot be changed");
  Plan scratch;
  ValidationRng rng;
  if (auto err = compileScript(json, rng, scratch)) return failCmd("invalid_script", err->path, err->message);
  if (!get(name) && names().size() - 1 >= kMaxUserScripts) return failCmd("limit_reached", "", "at most 8 user scripts");
  if (!blobs_.put(kBlobPrefix + name, json)) return failCmd("storage_error", "", "could not save the script (storage full?)");
  CmdResult r;
  r.ok = true;
  return r;
}

CmdResult ScriptLibrary::remove(const std::string& name) {
  if (isBuiltin(name)) return failCmd("read_only", "name", "the built-in script cannot be removed");
  if (!get(name)) return failCmd("not_found", "name", "no such script");
  blobs_.remove(kBlobPrefix + name);
  CmdResult r;
  r.ok = true;
  return r;
}

}  // namespace jig
