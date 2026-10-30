#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "bluetooth_control.h"
#include "controller.h"
#include "platform.h"

namespace jig {

constexpr uint32_t kMinPairingSec = 30;
constexpr uint32_t kMaxPairingSec = 600;
constexpr uint32_t kDefaultPairingSec = 180;  // default length of the Discovery window (configurable)
constexpr size_t kMaxPeerLabelLen = 32;
// The stack keeps kStackBondSlots bonds and, when full, silently evicts the OLDEST one to make room. Refused
// pairings briefly occupy a slot (the bond is written before we learn who it is), so the owner's computers are
// capped well below the slot count: an intruder can then never push a real bond out.
constexpr size_t kMaxPairedComputers = 3;
constexpr size_t kStackBondSlots = 5;

// Decide, once a link has finished pairing and its identity is known, whether it must be dropped.
//  known:       the computer was already bonded before this link came up
//  pairingOpen: the owner's window is open
//  knownBonds:  how many computers were bonded before this link came up
// A closed window refuses every unknown computer, whether or not it asked to bond.
inline bool refuseNewPeer(bool known, bool pairingOpen, size_t knownBonds) {
  if (known) return false;
  if (!pairingOpen) return true;
  return knownBonds >= kMaxPairedComputers;
}

// What the core needs from the Bluetooth stack. Addresses are lowercase "aa:bb:cc:dd:ee:ff" identity addresses.
struct BleLink {
  virtual ~BleLink() = default;
  virtual std::vector<std::string> bondedAddresses() = 0;
  virtual std::vector<std::string> connectedAddresses() = 0;
  virtual void disconnectAll() = 0;
  virtual bool forgetBond(const std::string& address) = 0;
  virtual void forgetAllBonds() = 0;
  virtual void setEnabled(bool on) = 0;         // false = radio silent: not visible, no link possible
  virtual void setPairingOpen(bool open) = 0;   // false = refuse (and undo) new bonds
  // True once after the computer ended a connection on purpose (its "Disconnect" button), not after a
  // link loss or one we ended ourselves.
  virtual bool takePeerDisconnectRequest() = 0;
};

struct Peer {
  std::string id;       // address without colons
  std::string address;
  std::string label;
  bool connected = false;
};

// Off: Bluetooth switched off. Waiting: on, only paired computers can find and reach the device. Discovery: a
// pairing window is open. Connected: a computer is linked (Discovery wins while a window is open).
enum class BluetoothState { Off, Waiting, Discovery, Connected };
const char* bluetoothStateName(BluetoothState state);

struct BluetoothSettings {
  bool enabled = true;
  uint32_t discoverySeconds = kDefaultPairingSec;  // 30..600
  bool offWhenComputerDisconnects = false;         // when set, the computer's own "Disconnect" switches Bluetooth off
};

struct BluetoothStatus {
  BluetoothState state = BluetoothState::Waiting;
  BluetoothSettings settings;
  bool connected = false;
  bool pairingOpen = false;
  std::optional<uint32_t> pairingRemainingMs;       // only while a timed window is open
  std::optional<std::string> connectedLabel;
  std::optional<uint32_t> connectedForMs;
  std::vector<Peer> peers;                          // paired computers
};

// Pairing policy and bookkeeping for the computers that use the device.
//  - Pairing (Discovery) is ALWAYS closed, even on a board that has never been paired and after "forget all". The
//    only way to open it is an explicit act by the owner: the dashboard/API/MCP, a schedule the owner wrote, or the
//    button on the board.
//  - A window closes by itself when its time is up, when someone pairs, or when the owner stops it.
//  - Switching Bluetooth off (dashboard, a schedule, or the computer's own "Disconnect" when the setting asks for
//    it) drops the link and silences the radio until it is switched on again. The choice is stored.
// Thread-safe. The firmware enforces `setPairingOpen`/`setEnabled` on the real stack.
class BluetoothManager : public BluetoothControl {
 public:
  BluetoothManager(Platform& platform, BleLink& link, BlobStore& blobs);

  void begin();
  void tick();  // closes an expired window and re-applies the policy
  BluetoothStatus status();

  CmdResult startPairing(uint32_t seconds);  // 0 = the configured length, else 30..600; turns Bluetooth on; errors: invalid_seconds
  CmdResult stopPairing();
  CmdResult enable();   // Bluetooth on
  CmdResult disable();  // Bluetooth off: drops the link, closes any window
  CmdResult updateSettings(std::optional<uint32_t> discoverySeconds, std::optional<bool> offWhenComputerDisconnects);  // errors: invalid_seconds, storage_error
  BluetoothSettings settings();
  CmdResult enableBluetooth() override { return enable(); }
  CmdResult disableBluetooth() override { return disable(); }
  CmdResult openDiscovery() override { return startPairing(0); }
  CmdResult forget(const std::string& id);   // errors: not_found
  CmdResult forgetAll();
  CmdResult rename(const std::string& id, const std::string& label);  // errors: invalid_label, not_found

 private:
  struct Locked;  // helpers that expect mutex_ to be held
  void applyLocked();
  void expireLocked();  // closes a finished/used pairing window
  bool pairingOpenLocked();
  std::string labelForLocked(const std::string& id);
  bool saveLabelsLocked();

  Platform& platform_;
  BleLink& link_;
  BlobStore& blobs_;
  std::mutex mutex_;
  std::map<std::string, std::string> labels_;      // id -> label
  std::map<std::string, uint32_t> connectedSince_;  // address -> millis of first sight (status() is polled)
  bool labelsDirty_ = false;  // default names handed out since the last save; tick() persists them
  BluetoothSettings settings_;
  bool saveSettingsLocked();
  bool windowOpen_ = false;
  uint32_t windowUntilMs_ = 0;
  std::vector<std::string> bondedAtOpen_;  // bonds when the window opened: a new one means someone paired
};

std::string peerId(const std::string& address);  // "AA:BB:.." -> "aabb.."
std::string bluetoothToJson(const BluetoothStatus& status);
std::string bluetoothSummaryJson(const BluetoothStatus& status);  // the compact object embedded in /api/status

}  // namespace jig
