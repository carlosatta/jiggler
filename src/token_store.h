#pragma once

#include <string>

#include "platform.h"

class TokenStore : public jig::TokenVault {
 public:
  std::string load() override;  // empty if never saved
  bool save(const std::string& token) override;
};
