#pragma once

#include "network_settings.h"

class NvsNetworkStore : public jig::NetworkStore {
 public:
  jig::NetworkSettings load() override;
  void save(const jig::NetworkSettings& settings) override;
};
