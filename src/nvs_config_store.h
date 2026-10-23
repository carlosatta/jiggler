#pragma once

#include "platform.h"

class NvsConfigStore : public jig::ConfigStore {
 public:
  jig::Config load() override;
  void save(const jig::Config& config) override;
};
