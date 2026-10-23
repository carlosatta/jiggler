#pragma once

#include <optional>
#include <string>
#include <vector>

#include "controller.h"
#include "movement.h"
#include "platform.h"

namespace jig {

constexpr size_t kMaxScriptBytes = 3000;
constexpr size_t kMaxScriptSteps = 2000;
constexpr size_t kMaxUserScripts = 8;

struct CompileError {
  std::string path;  // e.g. "/steps/2/dx"; empty for whole-script errors
  std::string message;
};

// Compiles {"steps":[...]} into a Plan. `out` is only written on success. Limits (steps, total wait counted
// at its worst case, nesting, coordinates, text) are enforced here, so a script that compiles once can
// never exceed them at run time.
std::optional<CompileError> compileScript(const std::string& json, Rng& rng, Plan& out);

class ScriptLibrary {
 public:
  explicit ScriptLibrary(BlobStore& blobs);

  std::vector<std::string> names() const;  // built-in first, then user scripts sorted
  bool isBuiltin(const std::string& name) const;
  std::optional<std::string> get(const std::string& name) const;
  CmdResult put(const std::string& name, const std::string& json);  // errors: invalid_script, read_only, limit_reached
  CmdResult remove(const std::string& name);                        // errors: read_only, not_found

 private:
  BlobStore& blobs_;
};

}  // namespace jig
