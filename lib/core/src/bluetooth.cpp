#include "bluetooth.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>

namespace jig {

namespace {

// Time left until `until`, 0 once it has passed (wrap-safe, never a huge number).
uint32_t msUntil(uint32_t until, uint32_t now) {
  const int32_t left = static_cast<int32_t>(until - now);
  return left > 0 ? static_cast<uint32_t>(left) : 0;
}

constexpr const char* kLabelsKey = "peers";

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

CmdResult fail(const char* error, const std::string& message) {
  CmdResult r;
  r.error = error;
  r.message = message;
  return r;
}

CmdResult ok() {
  CmdResult r;
  r.ok = true;
  return r;
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
  return s.substr(a, b - a);
}

}  // namespace

std::string peerId(const std::string& address) {
  std::string out;
  for (char c : address)
    if (c != ':') out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

BluetoothManager::BluetoothManager(Platform& platform, BleLink& link, BlobStore& blobs)
    : platform_(platform), link_(link), blobs_(blobs) {}

void BluetoothManager::begin() {
  std::lock_guard<std::mutex> lock(mutex_);
  labels_.clear();
  const std::string raw = blobs_.get(kLabelsKey);
  if (!raw.empty()) {
    JsonDocument doc;
    if (!deserializeJson(doc, raw) && doc.is<JsonObject>())
      for (JsonPair kv : doc.as<JsonObject>())
        if (kv.value().is<const char*>()) labels_[kv.key().c_str()] = kv.value().as<std::string>();
  }
  applyLocked();
}

void BluetoothManager::tick() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (labelsDirty_ && saveLabelsLocked()) labelsDirty_ = false;
  expireLocked();
  if (link_.takePeerDisconnectRequest() && !held_ && !windowOpen_ && !link_.bondedAddresses().empty()) {
    // The computer ended the connection on purpose. Left visible it would reconnect at once.
    held_ = true;
    heldUntilMs_ = platform_.nowMs() + kDisconnectHoldSec * 1000;
    connectedSince_.clear();
  }
  applyLocked();
}

void BluetoothManager::expireLocked() {
  const uint32_t now = platform_.nowMs();
  bool changed = false;
  if (windowOpen_) {
    bool someonePaired = false;
    for (const std::string& a : link_.bondedAddresses())
      if (std::find(bondedAtOpen_.begin(), bondedAtOpen_.end(), a) == bondedAtOpen_.end()) someonePaired = true;
    if (someonePaired || static_cast<int32_t>(now - windowUntilMs_) >= 0) {
      windowOpen_ = false;
      changed = true;
    }
  }
  if (held_ && static_cast<int32_t>(now - heldUntilMs_) >= 0) {
    held_ = false;
    changed = true;
  }
  if (changed) applyLocked();
}

bool BluetoothManager::pairingOpenLocked() { return windowOpen_; }

void BluetoothManager::applyLocked() {
  link_.setAdvertising(!held_);
  link_.setPairingOpen(pairingOpenLocked());
}

std::string BluetoothManager::labelForLocked(const std::string& id) {
  auto it = labels_.find(id);
  if (it != labels_.end()) return it->second;
  // the lowest "Computer N" not in use, so a name is never handed out twice
  size_t n = 1;
  std::string label;
  for (;; ++n) {
    label = "Computer " + std::to_string(n);
    const bool used = std::any_of(labels_.begin(), labels_.end(), [&](const auto& kv) { return kv.second == label; });
    if (!used) break;
  }
  labels_[id] = label;
  labelsDirty_ = true;  // status() is a read path (HTTP task): persisting waits for tick()
  return label;
}

bool BluetoothManager::saveLabelsLocked() {
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  for (const auto& kv : labels_) o[kv.first] = kv.second;
  std::string out;
  serializeJson(doc, out);
  return blobs_.put(kLabelsKey, out);
}

BluetoothStatus BluetoothManager::status() {
  std::lock_guard<std::mutex> lock(mutex_);
  expireLocked();
  const uint32_t now = platform_.nowMs();

  BluetoothStatus s;
  const std::vector<std::string> bonded = link_.bondedAddresses();
  const std::vector<std::string> connected = link_.connectedAddresses();
  s.held = held_;
  if (held_) s.heldRemainingMs = msUntil(heldUntilMs_, now);
  s.pairingOpen = windowOpen_;
  if (windowOpen_) s.pairingRemainingMs = msUntil(windowUntilMs_, now);

  // forget connections that ended without a callback, learn ones that began without one
  for (auto it = connectedSince_.begin(); it != connectedSince_.end();)
    it = std::find(connected.begin(), connected.end(), it->first) == connected.end() ? connectedSince_.erase(it) : std::next(it);
  for (const std::string& a : connected)
    if (!connectedSince_.count(a)) connectedSince_[a] = now;

  for (const std::string& address : bonded) {
    Peer p;
    p.address = address;
    p.id = peerId(address);
    p.label = labelForLocked(p.id);
    p.connected = std::find(connected.begin(), connected.end(), address) != connected.end();
    s.peers.push_back(p);
  }

  s.connected = !connected.empty();
  if (s.connected) {
    const std::string& first = connected.front();
    auto it = std::find_if(s.peers.begin(), s.peers.end(), [&](const Peer& p) { return p.address == first; });
    s.connectedLabel = it != s.peers.end() ? it->label : std::string("Nuovo computer");
    s.connectedForMs = now - connectedSince_[first];
  }
  return s;
}

CmdResult BluetoothManager::startPairing(uint32_t seconds) {
  if (seconds < kMinPairingSec || seconds > kMaxPairingSec)
    return fail("invalid_seconds", "the pairing window must last between 30 and 600 seconds");
  std::lock_guard<std::mutex> lock(mutex_);
  held_ = false;
  windowOpen_ = true;
  windowUntilMs_ = platform_.nowMs() + seconds * 1000;
  bondedAtOpen_ = link_.bondedAddresses();
  applyLocked();
  return ok();
}

CmdResult BluetoothManager::stopPairing() {
  std::lock_guard<std::mutex> lock(mutex_);
  windowOpen_ = false;
  applyLocked();
  return ok();
}

CmdResult BluetoothManager::disconnect() {
  std::lock_guard<std::mutex> lock(mutex_);
  held_ = true;
  heldUntilMs_ = platform_.nowMs() + kDisconnectHoldSec * 1000;
  windowOpen_ = false;  // a hidden device cannot be paired with, so an open window would only lie
  link_.disconnectAll();
  connectedSince_.clear();
  applyLocked();
  return ok();
}

CmdResult BluetoothManager::reconnect() {
  std::lock_guard<std::mutex> lock(mutex_);
  held_ = false;
  applyLocked();
  return ok();
}

CmdResult BluetoothManager::forget(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const std::string& address : link_.bondedAddresses()) {
    if (peerId(address) != id) continue;
    const std::vector<std::string> connected = link_.connectedAddresses();
    link_.forgetBond(address);
    bondedAtOpen_.erase(std::remove(bondedAtOpen_.begin(), bondedAtOpen_.end(), address), bondedAtOpen_.end());
    // removing a bond does not end the link on the real stack: drop it, or the computer stays connected unlisted
    if (std::find(connected.begin(), connected.end(), address) != connected.end()) link_.disconnectAll();
    labels_.erase(id);
    connectedSince_.erase(address);
    if (!saveLabelsLocked()) labelsDirty_ = true;
    applyLocked();
    return ok();
  }
  return fail("not_found", "no such paired computer");
}

CmdResult BluetoothManager::forgetAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  link_.forgetAllBonds();
  link_.disconnectAll();
  labels_.clear();
  connectedSince_.clear();
  if (!saveLabelsLocked()) labelsDirty_ = true;
  applyLocked();
  return ok();
}

CmdResult BluetoothManager::rename(const std::string& id, const std::string& rawLabel) {
  const std::string label = trim(rawLabel);
  if (label.empty() || label.size() > kMaxPeerLabelLen) return fail("invalid_label", "the name must be 1 to 32 characters");
  for (unsigned char c : label)
    if (c < 0x20 || c == 0x7F) return fail("invalid_label", "the name must not contain control characters");
  std::lock_guard<std::mutex> lock(mutex_);
  for (const std::string& address : link_.bondedAddresses()) {
    if (peerId(address) != id) continue;
    const std::string previous = labels_.count(id) ? labels_[id] : std::string();
    labels_[id] = label;
    if (!saveLabelsLocked()) {
      if (previous.empty()) labels_.erase(id); else labels_[id] = previous;
      return fail("storage_error", "could not save the name (storage full?)");
    }
    labelsDirty_ = false;
    return ok();
  }
  return fail("not_found", "no such paired computer");
}

namespace {
void fillComputer(JsonObject o, const BluetoothStatus& s) {
  o["label"] = *s.connectedLabel;
  o["connectedForMs"] = *s.connectedForMs;
}
void fillPairing(JsonObject o, const BluetoothStatus& s) {
  o["open"] = s.pairingOpen;
  if (s.pairingRemainingMs) o["remainingMs"] = *s.pairingRemainingMs;
  else o["remainingMs"] = nullptr;
}
}  // namespace

std::string bluetoothToJson(const BluetoothStatus& s) {
  JsonDocument doc;
  doc["connected"] = s.connected;
  doc["held"] = s.held;
  if (s.heldRemainingMs) doc["heldRemainingMs"] = *s.heldRemainingMs;
  else doc["heldRemainingMs"] = nullptr;
  fillPairing(doc["pairing"].to<JsonObject>(), s);
  if (s.connected) fillComputer(doc["computer"].to<JsonObject>(), s);
  else doc["computer"] = nullptr;
  JsonArray peers = doc["peers"].to<JsonArray>();
  for (const Peer& p : s.peers) {
    JsonObject o = peers.add<JsonObject>();
    o["id"] = p.id;
    o["address"] = p.address;
    o["label"] = p.label;
    o["connected"] = p.connected;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string bluetoothSummaryJson(const BluetoothStatus& s) {
  JsonDocument doc;
  doc["connected"] = s.connected;
  doc["held"] = s.held;
  if (s.heldRemainingMs) doc["heldRemainingMs"] = *s.heldRemainingMs;
  else doc["heldRemainingMs"] = nullptr;
  fillPairing(doc["pairing"].to<JsonObject>(), s);
  if (s.connected) fillComputer(doc["computer"].to<JsonObject>(), s);
  else doc["computer"] = nullptr;
  doc["peerCount"] = s.peers.size();
  std::string out;
  serializeJson(doc, out);
  return out;
}

}  // namespace jig
