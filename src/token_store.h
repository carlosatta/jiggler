#pragma once

#include <string>

class TokenStore {
 public:
  std::string load();  // empty if never saved
  void save(const std::string& token);
};
