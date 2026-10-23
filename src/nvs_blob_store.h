#pragma once

#include "platform.h"

// BlobStore over NVS (namespace "jiggler"). Arbitrary key names are stored under a short hashed NVS
// key ("b" + 8 hex of FNV-1a) because NVS keys are limited to 15 characters; an index blob under the
// raw key "_idx" lists the real key names so keys(prefix) works. Values must stay under ~3900 bytes.
class NvsBlobStore : public jig::BlobStore {
 public:
  std::string get(const std::string& key) override;
  void put(const std::string& key, const std::string& value) override;
  void remove(const std::string& key) override;
  std::vector<std::string> keys(const std::string& prefix) override;
};
