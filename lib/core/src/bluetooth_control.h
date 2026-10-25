#pragma once

#include "controller.h"

namespace jig {

// What a schedule (or any other automation) may ask of the Bluetooth radio.
struct BluetoothControl {
  virtual ~BluetoothControl() = default;
  virtual CmdResult enableBluetooth() = 0;
  virtual CmdResult disableBluetooth() = 0;
  virtual CmdResult openDiscovery() = 0;  // a pairing window of the configured length (turns Bluetooth on first)
};

}  // namespace jig
