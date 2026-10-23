#include <unity.h>

#include <ArduinoJson.h>

#include <algorithm>
#include <string>

#include "api_ex.h"
#include "bluetooth.h"
#include "controller.h"
#include "fakes.h"

using namespace jig;

void setUp() {}
void tearDown() {}

namespace {

const char* kA = "aa:bb:cc:dd:ee:01";
const char* kB = "aa:bb:cc:dd:ee:02";
const char* kIdA = "aabbccddee01";
const char* kIdB = "aabbccddee02";

struct FakeBleLink : BleLink {
  std::vector<std::string> bonded, connected;
  bool advertising = true;
  bool pairingOpen = false;
  bool peerRequestedDisconnect = false;
  int disconnects = 0;
  std::vector<std::string> bondedAddresses() override { return bonded; }
  std::vector<std::string> connectedAddresses() override { return connected; }
  void disconnectAll() override {
    disconnects++;
    connected.clear();
  }
  bool forgetBond(const std::string& a) override {
    auto it = std::find(bonded.begin(), bonded.end(), a);
    if (it == bonded.end()) return false;
    bonded.erase(it);
    return true;
  }
  void forgetAllBonds() override { bonded.clear(); }
  void setAdvertising(bool on) override { advertising = on; }
  void setPairingOpen(bool open) override { pairingOpen = open; }
  bool takePeerDisconnectRequest() override {
    const bool b = peerRequestedDisconnect;
    peerRequestedDisconnect = false;
    return b;
  }
};

struct Env {
  FakeClock clock;
  FakeBleLink link;
  FlakyBlobStore blobs;
  BluetoothManager bt{clock, link, blobs};
  FlakyBlobStore* flaky() { return &blobs; }
  Env(std::vector<std::string> bonded = {}) {
    link.bonded = std::move(bonded);
    bt.begin();
  }
};

JsonDocument parse(const std::string& s) {
  JsonDocument doc;
  TEST_ASSERT_TRUE_MESSAGE(!deserializeJson(doc, s), s.c_str());
  return doc;
}

struct Api {
  FakeClock clock;
  XorShiftRng rng{1};
  MemStore config;
  Controller controller{clock, rng, config};
  FakeBleLink link;
  MemBlobStore blobs;
  BluetoothManager bt{clock, link, blobs};
  struct Vault : TokenVault {
    std::string token = "token-iniziale";
    bool fail = false;
    std::string load() override { return token; }
    bool save(const std::string& t) override {
      if (fail) return false;
      token = t;
      return true;
    }
  } vault;
  ApiContext ctx{controller};
  Api(std::vector<std::string> bonded = {kA}) {
    controller.begin();
    link.bonded = std::move(bonded);
    bt.begin();
    ctx.bluetooth = &bt;
    ctx.tokens = &vault;
  }
  HttpResponse call(const std::string& method, const std::string& path, const std::string& body = "") {
    return handleApiEx(ctx, method, path, body);
  }
};

}  // namespace

void test_first_run_pairing_is_open_without_timeout() {
  Env e;
  BluetoothStatus s = e.bt.status();
  TEST_ASSERT_TRUE(s.pairingOpen);
  TEST_ASSERT_TRUE(s.firstRun);
  TEST_ASSERT_FALSE(s.pairingRemainingMs.has_value());
  TEST_ASSERT_TRUE(e.link.pairingOpen);
  TEST_ASSERT_TRUE(s.peers.empty());
}

void test_with_a_bond_pairing_is_closed() {
  Env e({kA});
  BluetoothStatus s = e.bt.status();
  TEST_ASSERT_FALSE(s.pairingOpen);
  TEST_ASSERT_FALSE(s.firstRun);
  TEST_ASSERT_FALSE(e.link.pairingOpen);
}

void test_pairing_window_counts_down_and_closes() {
  Env e({kA});
  e.clock.now = 1000;
  TEST_ASSERT_TRUE(e.bt.startPairing(120).ok);
  BluetoothStatus s = e.bt.status();
  TEST_ASSERT_TRUE(s.pairingOpen);
  TEST_ASSERT_EQUAL_UINT32(120000, *s.pairingRemainingMs);
  TEST_ASSERT_TRUE(e.link.pairingOpen);
  e.clock.now = 61000;
  TEST_ASSERT_EQUAL_UINT32(60000, *e.bt.status().pairingRemainingMs);
  e.clock.now = 120999;
  e.bt.tick();
  TEST_ASSERT_TRUE(e.link.pairingOpen);
  e.clock.now = 121000;
  e.bt.tick();
  TEST_ASSERT_FALSE(e.link.pairingOpen);
  TEST_ASSERT_FALSE(e.bt.status().pairingOpen);
}

void test_pairing_window_survives_clock_wraparound() {
  Env e({kA});
  e.clock.now = 0xFFFFFF00u;
  TEST_ASSERT_TRUE(e.bt.startPairing(30).ok);
  const uint32_t t0 = e.clock.now;
  e.clock.now = t0 + 29999;
  e.bt.tick();
  TEST_ASSERT_TRUE(e.link.pairingOpen);
  e.clock.now = t0 + 30000;
  e.bt.tick();
  TEST_ASSERT_FALSE(e.link.pairingOpen);
}

void test_pairing_seconds_are_bounded() {
  Env e({kA});
  for (uint32_t bad : {0u, 29u, 601u, 100000u}) {
    CmdResult r = e.bt.startPairing(bad);
    TEST_ASSERT_FALSE_MESSAGE(r.ok, std::to_string(bad).c_str());
    TEST_ASSERT_EQUAL_STRING("invalid_seconds", r.error.c_str());
  }
  TEST_ASSERT_TRUE(e.bt.startPairing(30).ok);
  TEST_ASSERT_TRUE(e.bt.startPairing(600).ok);
}

void test_stop_pairing_closes_the_window() {
  Env e({kA});
  e.bt.startPairing(120);
  TEST_ASSERT_TRUE(e.bt.stopPairing().ok);
  TEST_ASSERT_FALSE(e.bt.status().pairingOpen);
  TEST_ASSERT_FALSE(e.link.pairingOpen);
}

void test_disconnect_holds_the_link_until_reconnect() {
  Env e({kA});
  e.link.connected = {kA};
  TEST_ASSERT_TRUE(e.bt.disconnect().ok);
  TEST_ASSERT_EQUAL_INT(1, e.link.disconnects);
  TEST_ASSERT_FALSE(e.link.advertising);
  BluetoothStatus s = e.bt.status();
  TEST_ASSERT_TRUE(s.held);
  TEST_ASSERT_FALSE(s.connected);
  e.bt.tick();
  TEST_ASSERT_FALSE(e.link.advertising);  // stays hidden: the computer cannot reconnect behind your back
  TEST_ASSERT_TRUE(e.bt.reconnect().ok);
  TEST_ASSERT_TRUE(e.link.advertising);
  TEST_ASSERT_FALSE(e.bt.status().held);
}

void test_starting_pairing_releases_a_hold() {
  Env e({kA});
  e.bt.disconnect();
  TEST_ASSERT_TRUE(e.bt.startPairing(60).ok);
  TEST_ASSERT_TRUE(e.link.advertising);
  TEST_ASSERT_FALSE(e.bt.status().held);
}

void test_peers_get_stable_default_labels_and_can_be_renamed() {
  Env e({kA, kB});
  BluetoothStatus s = e.bt.status();
  TEST_ASSERT_EQUAL_UINT32(2, s.peers.size());
  TEST_ASSERT_EQUAL_STRING(kIdA, s.peers[0].id.c_str());
  TEST_ASSERT_EQUAL_STRING("Computer 1", s.peers[0].label.c_str());
  TEST_ASSERT_EQUAL_STRING("Computer 2", s.peers[1].label.c_str());

  TEST_ASSERT_TRUE(e.bt.rename(kIdA, "MacBook di Carlo").ok);
  BluetoothManager again(e.clock, e.link, e.blobs);
  again.begin();
  TEST_ASSERT_EQUAL_STRING("MacBook di Carlo", again.status().peers[0].label.c_str());
  TEST_ASSERT_EQUAL_STRING("Computer 2", again.status().peers[1].label.c_str());
}

void test_rename_validation() {
  Env e({kA});
  for (const char* bad : {"", "   ", "tab\there"}) {
    CmdResult r = e.bt.rename(kIdA, bad);
    TEST_ASSERT_FALSE_MESSAGE(r.ok, bad);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("invalid_label", r.error.c_str(), bad);
  }
  TEST_ASSERT_FALSE(e.bt.rename(kIdA, std::string(33, 'x')).ok);
  TEST_ASSERT_TRUE(e.bt.rename(kIdA, std::string(32, 'x')).ok);
  CmdResult unknown = e.bt.rename("000000000000", "x");
  TEST_ASSERT_EQUAL_STRING("not_found", unknown.error.c_str());
  TEST_ASSERT_EQUAL_STRING(std::string(32, 'x').c_str(), e.bt.status().peers[0].label.c_str());
}

void test_forget_one_and_all() {
  Env e({kA, kB});
  TEST_ASSERT_TRUE(e.bt.forget(kIdA).ok);
  TEST_ASSERT_EQUAL_UINT32(1, e.link.bonded.size());
  TEST_ASSERT_EQUAL_STRING(kB, e.link.bonded[0].c_str());
  CmdResult unknown = e.bt.forget(kIdA);
  TEST_ASSERT_EQUAL_STRING("not_found", unknown.error.c_str());

  TEST_ASSERT_TRUE(e.bt.forgetAll().ok);
  TEST_ASSERT_TRUE(e.link.bonded.empty());
  BluetoothStatus s = e.bt.status();
  TEST_ASSERT_FALSE(s.firstRun);         // a board that has been paired is not factory-new: pairing stays closed
  TEST_ASSERT_FALSE(s.pairingOpen);
  TEST_ASSERT_FALSE(e.link.pairingOpen);
  TEST_ASSERT_TRUE(s.peers.empty());
}

void test_connected_computer_and_duration() {
  Env e({kA, kB});
  e.link.connected = {kB};
  e.clock.now = 1000;
  e.bt.status();  // first sight of the connection
  e.clock.now = 61000;
  BluetoothStatus s = e.bt.status();
  TEST_ASSERT_TRUE(s.connected);
  TEST_ASSERT_EQUAL_STRING("Computer 2", s.connectedLabel->c_str());
  TEST_ASSERT_EQUAL_UINT32(60000, *s.connectedForMs);
  TEST_ASSERT_FALSE(s.peers[0].connected);
  TEST_ASSERT_TRUE(s.peers[1].connected);

  e.link.connected.clear();
  s = e.bt.status();
  TEST_ASSERT_FALSE(s.connected);
  TEST_ASSERT_FALSE(s.connectedLabel.has_value());
  TEST_ASSERT_FALSE(s.connectedForMs.has_value());
}

void test_connection_unknown_to_the_manager_counts_from_first_sight() {
  Env e({kA});
  e.link.connected = {kA};  // e.g. connected before the manager saw the callback
  e.clock.now = 5000;
  TEST_ASSERT_EQUAL_UINT32(0, *e.bt.status().connectedForMs);
  e.clock.now = 9000;
  TEST_ASSERT_EQUAL_UINT32(4000, *e.bt.status().connectedForMs);
}

void test_unbonded_connection_is_shown_with_its_address() {
  Env e;  // first run: a computer is connected but its bond is not in the list yet
  e.link.connected = {kA};
  BluetoothStatus s = e.bt.status();
  TEST_ASSERT_TRUE(s.connected);
  TEST_ASSERT_TRUE(s.connectedLabel.has_value());
}

void test_bluetooth_routes() {
  Api a;
  HttpResponse g = a.call("GET", "/api/bluetooth");
  TEST_ASSERT_EQUAL_INT(200, g.status);
  JsonDocument d = parse(g.body);
  TEST_ASSERT_FALSE(d["connected"].as<bool>());
  TEST_ASSERT_FALSE(d["held"].as<bool>());
  TEST_ASSERT_FALSE(d["pairing"]["open"].as<bool>());
  TEST_ASSERT_TRUE(g.body.find("\"remainingMs\":null") != std::string::npos);
  TEST_ASSERT_TRUE(g.body.find("\"computer\":null") != std::string::npos);
  TEST_ASSERT_EQUAL_STRING(kIdA, d["peers"][0]["id"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING(kA, d["peers"][0]["address"].as<const char*>());

  HttpResponse p = a.call("POST", "/api/bluetooth/pairing", "{\"seconds\":90}");
  TEST_ASSERT_EQUAL_INT(200, p.status);
  TEST_ASSERT_TRUE(parse(p.body)["pairing"]["open"].as<bool>());
  TEST_ASSERT_EQUAL_UINT32(90000, parse(p.body)["pairing"]["remainingMs"].as<uint32_t>());
  TEST_ASSERT_EQUAL_INT(200, a.call("POST", "/api/bluetooth/pairing").status);  // default window
  TEST_ASSERT_EQUAL_UINT32(180000, parse(a.call("GET", "/api/bluetooth").body)["pairing"]["remainingMs"].as<uint32_t>());
  TEST_ASSERT_FALSE(parse(a.call("DELETE", "/api/bluetooth/pairing").body)["pairing"]["open"].as<bool>());

  HttpResponse bad = a.call("POST", "/api/bluetooth/pairing", "{\"seconds\":5}");
  TEST_ASSERT_EQUAL_INT(400, bad.status);
  TEST_ASSERT_EQUAL_STRING("invalid_seconds", parse(bad.body)["error"].as<const char*>());
  for (const char* body : {"{", "{\"seconds\":\"x\"}", "{\"seconds\":1.5}", "[]"})
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, a.call("POST", "/api/bluetooth/pairing", body).status, body);

  TEST_ASSERT_TRUE(parse(a.call("POST", "/api/bluetooth/disconnect").body)["held"].as<bool>());
  TEST_ASSERT_FALSE(parse(a.call("POST", "/api/bluetooth/reconnect").body)["held"].as<bool>());

  HttpResponse ren = a.call("PUT", std::string("/api/bluetooth/peers/") + kIdA, "{\"label\":\"Studio\"}");
  TEST_ASSERT_EQUAL_INT(200, ren.status);
  TEST_ASSERT_EQUAL_STRING("Studio", parse(ren.body)["peers"][0]["label"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(400, a.call("PUT", std::string("/api/bluetooth/peers/") + kIdA, "{\"label\":\"\"}").status);
  TEST_ASSERT_EQUAL_INT(400, a.call("PUT", std::string("/api/bluetooth/peers/") + kIdA, "{}").status);
  TEST_ASSERT_EQUAL_INT(404, a.call("PUT", "/api/bluetooth/peers/000000000000", "{\"label\":\"x\"}").status);

  TEST_ASSERT_EQUAL_INT(404, a.call("DELETE", "/api/bluetooth/peers/000000000000").status);
  TEST_ASSERT_EQUAL_INT(200, a.call("DELETE", std::string("/api/bluetooth/peers/") + kIdA).status);
  TEST_ASSERT_TRUE(a.link.bonded.empty());
  TEST_ASSERT_EQUAL_INT(200, a.call("DELETE", "/api/bluetooth/peers").status);

  TEST_ASSERT_EQUAL_INT(405, a.call("PUT", "/api/bluetooth").status);
  TEST_ASSERT_EQUAL_INT(405, a.call("GET", "/api/bluetooth/disconnect").status);
  TEST_ASSERT_EQUAL_INT(404, a.call("GET", "/api/bluetooth/nope").status);
  a.ctx.bluetooth = nullptr;
  TEST_ASSERT_EQUAL_INT(404, a.call("GET", "/api/bluetooth").status);
  TEST_ASSERT_EQUAL_INT(404, a.call("POST", "/api/bluetooth/pairing").status);
}

void test_status_carries_a_bluetooth_summary() {
  Api a;
  a.link.connected = {kA};
  a.clock.now = 2000;
  a.bt.status();  // first sight of the connection
  a.clock.now = 7000;
  JsonDocument d = parse(a.call("GET", "/api/status").body);
  TEST_ASSERT_TRUE(d["bluetooth"]["connected"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("Computer 1", d["bluetooth"]["computer"]["label"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(5000, d["bluetooth"]["computer"]["connectedForMs"].as<uint32_t>());
  TEST_ASSERT_FALSE(d["bluetooth"]["pairing"]["open"].as<bool>());
  TEST_ASSERT_EQUAL_UINT32(1, d["bluetooth"]["peerCount"].as<uint32_t>());
  TEST_ASSERT_EQUAL_STRING("running", d["state"].as<const char*>());  // the original fields are untouched

  a.ctx.bluetooth = nullptr;
  TEST_ASSERT_TRUE(a.call("GET", "/api/status").body.find("bluetooth") == std::string::npos);
}

void test_change_token() {
  Api a;
  const std::string route = "/api/token";
  HttpResponse ok = a.call("PUT", route, "{\"current\":\"token-iniziale\",\"token\":\"nuovo-token-1\"}");
  TEST_ASSERT_EQUAL_INT(200, ok.status);
  TEST_ASSERT_TRUE(parse(ok.body)["changed"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("nuovo-token-1", a.vault.token.c_str());

  HttpResponse wrong = a.call("PUT", route, "{\"current\":\"sbagliato\",\"token\":\"altro-token-2\"}");
  TEST_ASSERT_EQUAL_INT(403, wrong.status);
  TEST_ASSERT_EQUAL_STRING("wrong_current", parse(wrong.body)["error"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("nuovo-token-1", a.vault.token.c_str());

  const struct {
    const char* body;
    const char* field;
  } bad[] = {
      {"{\"current\":\"nuovo-token-1\",\"token\":\"corto\"}", "token"},
      {"{\"current\":\"nuovo-token-1\",\"token\":\"con spazio dentro\"}", "token"},
      {"{\"current\":\"nuovo-token-1\",\"token\":\"àccènti-nonvalidi\"}", "token"},
      {"{\"current\":\"nuovo-token-1\",\"token\":\"1234567890123456789012345678901234567890x\"}", "token"},
      {"{\"current\":\"nuovo-token-1\"}", "token"},
      {"{\"token\":\"valido-token-3\"}", "current"},
      {"{\"current\":5,\"token\":\"valido-token-3\"}", "current"},
  };
  for (const auto& c : bad) {
    HttpResponse r = a.call("PUT", route, c.body);
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, r.status, c.body);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.field, parse(r.body)["field"].as<const char*>(), c.body);
  }
  TEST_ASSERT_EQUAL_INT(400, a.call("PUT", route, "{").status);
  TEST_ASSERT_EQUAL_STRING("nuovo-token-1", a.vault.token.c_str());

  a.vault.fail = true;
  HttpResponse full = a.call("PUT", route, "{\"current\":\"nuovo-token-1\",\"token\":\"valido-token-3\"}");
  TEST_ASSERT_EQUAL_INT(507, full.status);
  TEST_ASSERT_EQUAL_STRING("nuovo-token-1", a.vault.token.c_str());
  TEST_ASSERT_EQUAL_INT(405, a.call("GET", route).status);
  a.ctx.tokens = nullptr;
  TEST_ASSERT_EQUAL_INT(404, a.call("PUT", route, "{}").status);
}

void test_device_info_in_status() {
  Api a;
  a.ctx.info = {"1.2.3", "MX Master 3S", "00:1f:20:fd:3b:76"};
  JsonDocument d = parse(a.call("GET", "/api/status").body);
  TEST_ASSERT_EQUAL_STRING("1.2.3", d["device"]["version"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("MX Master 3S", d["device"]["bleName"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("00:1f:20:fd:3b:76", d["device"]["bleAddress"].as<const char*>());
}

void test_disconnect_closes_an_open_pairing_window() {
  Env e({kA});
  TEST_ASSERT_TRUE(e.bt.startPairing(120).ok);
  TEST_ASSERT_TRUE(e.bt.disconnect().ok);
  BluetoothStatus s = e.bt.status();
  TEST_ASSERT_TRUE(s.held);
  TEST_ASSERT_FALSE(s.pairingOpen);
  TEST_ASSERT_FALSE(e.link.pairingOpen);
  TEST_ASSERT_FALSE(s.pairingRemainingMs.has_value());
}

void test_rename_reports_a_full_storage() {
  Env e({kA});
  e.bt.status();
  e.flaky()->failPuts = true;
  CmdResult r = e.bt.rename(kIdA, "Ufficio");
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("storage_error", r.error.c_str());
}

void test_reading_the_status_never_writes_storage() {
  Env e({kA, kB});
  e.blobs.data.clear();
  e.bt.status();
  TEST_ASSERT_TRUE(e.blobs.data.empty());
}

void test_default_names_are_persisted_by_tick_not_by_status() {
  Env e({kA});
  e.bt.status();
  TEST_ASSERT_TRUE(e.blobs.data.empty());
  e.bt.tick();
  TEST_ASSERT_TRUE(e.blobs.data.count("peers") == 1);
}

void test_default_names_never_repeat_after_a_forget() {
  Env e({kA, kB});
  BluetoothStatus s = e.bt.status();
  TEST_ASSERT_EQUAL_STRING("Computer 1", s.peers[0].label.c_str());
  TEST_ASSERT_EQUAL_STRING("Computer 2", s.peers[1].label.c_str());
  e.bt.forget(kIdA);
  e.link.bonded.push_back("aa:bb:cc:dd:ee:03");
  s = e.bt.status();
  TEST_ASSERT_EQUAL_UINT32(2, s.peers.size());
  TEST_ASSERT_EQUAL_STRING("Computer 2", s.peers[0].label.c_str());
  TEST_ASSERT_EQUAL_STRING("Computer 1", s.peers[1].label.c_str());  // the lowest free number, never a duplicate
}

void test_forgetting_a_connected_computer_drops_its_link() {
  Env e({kA, kB});
  e.link.connected = {kA};
  TEST_ASSERT_TRUE(e.bt.forget(kIdA).ok);
  TEST_ASSERT_TRUE(e.link.connected.empty());
  TEST_ASSERT_FALSE(e.bt.status().connected);
}

void test_forget_all_while_connected_drops_the_link_and_keeps_pairing_closed() {
  Env e({kA});
  e.link.connected = {kA};
  TEST_ASSERT_TRUE(e.bt.forgetAll().ok);
  BluetoothStatus s = e.bt.status();
  TEST_ASSERT_FALSE(s.connected);
  TEST_ASSERT_FALSE(s.firstRun);
  TEST_ASSERT_FALSE(s.pairingOpen);
}

void test_refusal_policy() {
  // known computer: always allowed
  TEST_ASSERT_FALSE(refuseNewPeer(true, false, 3));
  // pairing closed: an unknown computer is refused whether or not it asked for a bond
  TEST_ASSERT_TRUE(refuseNewPeer(false, false, 1));
  // pairing open: allowed while there is room, refused once the paired limit is reached
  TEST_ASSERT_FALSE(refuseNewPeer(false, true, kMaxPairedComputers - 1));
  TEST_ASSERT_TRUE(refuseNewPeer(false, true, kMaxPairedComputers));
}

void test_pairing_window_is_three_minutes_by_default() {
  TEST_ASSERT_EQUAL_UINT32(180, kDefaultPairingSec);
  Env e({kA});
  TEST_ASSERT_TRUE(e.bt.startPairing(kDefaultPairingSec).ok);
  TEST_ASSERT_EQUAL_UINT32(180000, *e.bt.status().pairingRemainingMs);
}

void test_window_closes_by_itself_when_a_computer_pairs() {
  Env e({kA});
  e.bt.startPairing(kDefaultPairingSec);
  e.link.bonded.push_back(kB);  // a new computer finished pairing
  e.bt.tick();
  BluetoothStatus s = e.bt.status();
  TEST_ASSERT_FALSE(s.pairingOpen);
  TEST_ASSERT_FALSE(s.pairingRemainingMs.has_value());
  TEST_ASSERT_FALSE(e.link.pairingOpen);
}

void test_status_alone_also_notices_the_new_pairing() {
  Env e({kA});
  e.bt.startPairing(kDefaultPairingSec);
  e.link.bonded.push_back(kB);
  TEST_ASSERT_FALSE(e.bt.status().pairingOpen);  // no tick() in between: the dashboard polls status()
}

void test_window_stays_open_when_a_known_computer_reconnects() {
  Env e({kA});
  e.bt.startPairing(kDefaultPairingSec);
  e.link.connected = {kA};
  e.bt.tick();
  TEST_ASSERT_TRUE(e.bt.status().pairingOpen);
}

void test_stopping_the_window_by_hand_still_works() {
  Env e({kA});
  e.bt.startPairing(kDefaultPairingSec);
  TEST_ASSERT_TRUE(e.bt.stopPairing().ok);
  TEST_ASSERT_FALSE(e.bt.status().pairingOpen);
}

void test_disconnect_hold_lasts_three_minutes_then_the_computer_can_reconnect() {
  TEST_ASSERT_EQUAL_UINT32(180, kDisconnectHoldSec);
  Env e({kA});
  e.link.connected = {kA};
  e.bt.disconnect();
  BluetoothStatus s = e.bt.status();
  TEST_ASSERT_TRUE(s.held);
  TEST_ASSERT_EQUAL_UINT32(180000, *s.heldRemainingMs);
  TEST_ASSERT_FALSE(e.link.advertising);
  e.clock.now = 179999;
  e.bt.tick();
  TEST_ASSERT_TRUE(e.bt.status().held);
  e.clock.now = 180000;
  e.bt.tick();
  s = e.bt.status();
  TEST_ASSERT_FALSE(s.held);
  TEST_ASSERT_FALSE(s.heldRemainingMs.has_value());
  TEST_ASSERT_TRUE(e.link.advertising);  // visible again: the computer can be reconnected from its side
}

void test_hold_countdown_is_in_the_json() {
  Env e({kA});
  e.bt.disconnect();
  JsonDocument d;
  deserializeJson(d, bluetoothToJson(e.bt.status()));
  TEST_ASSERT_EQUAL_UINT32(180000, d["heldRemainingMs"].as<uint32_t>());
  JsonDocument s;
  deserializeJson(s, bluetoothSummaryJson(e.bt.status()));
  TEST_ASSERT_EQUAL_UINT32(180000, s["heldRemainingMs"].as<uint32_t>());
  e.bt.reconnect();
  deserializeJson(d, bluetoothToJson(e.bt.status()));
  TEST_ASSERT_TRUE(d["heldRemainingMs"].isNull());
}

void test_losing_every_bond_does_not_make_the_board_factory_new_again() {
  Env e({kA});
  e.bt.tick();                 // sees the bond: from now on this board has been paired
  e.link.bonded.clear();       // the stack dropped it (e.g. the computer forgot the device and re-paired)
  BluetoothStatus s = e.bt.status();
  TEST_ASSERT_FALSE(s.firstRun);
  TEST_ASSERT_FALSE(s.pairingOpen);
  TEST_ASSERT_FALSE(e.link.pairingOpen);
}

void test_the_paired_before_memory_survives_a_restart() {
  Env e({kA});
  e.bt.tick();
  e.link.bonded.clear();
  BluetoothManager again(e.clock, e.link, e.blobs);  // reboot with the same storage and no bonds left
  again.begin();
  TEST_ASSERT_FALSE(again.status().pairingOpen);
  TEST_ASSERT_FALSE(e.link.pairingOpen);
}

void test_a_board_with_stored_names_counts_as_paired_before() {
  // boards flashed before the flag existed: their saved computer names prove they were paired
  Env e({kA});
  e.bt.status();
  e.bt.tick();
  e.blobs.data.erase("paired");
  e.link.bonded.clear();
  BluetoothManager again(e.clock, e.link, e.blobs);
  again.begin();
  TEST_ASSERT_FALSE(again.status().pairingOpen);
}

void test_a_factory_new_board_stays_open_until_the_first_pairing() {
  Env e;
  e.bt.tick();
  TEST_ASSERT_TRUE(e.bt.status().pairingOpen);
  e.link.bonded.push_back(kA);  // the first computer pairs
  e.bt.tick();
  e.link.bonded.clear();
  TEST_ASSERT_FALSE(e.bt.status().pairingOpen);
}

void test_a_failed_save_of_the_paired_flag_is_retried() {
  Env e;
  e.flaky()->failPuts = true;
  e.link.bonded.push_back(kA);
  e.bt.tick();
  e.link.bonded.clear();
  TEST_ASSERT_FALSE(e.bt.status().pairingOpen);       // held in memory even though it could not be stored
  e.flaky()->failPuts = false;
  e.bt.tick();
  TEST_ASSERT_TRUE(e.blobs.data.count("paired") == 1);
}

void test_the_computer_disconnecting_hides_the_device_like_the_dashboard_does() {
  Env e({kA});
  e.link.connected = {kA};
  e.link.peerRequestedDisconnect = true;
  e.link.connected.clear();
  e.bt.tick();
  BluetoothStatus s = e.bt.status();
  TEST_ASSERT_TRUE(s.held);
  TEST_ASSERT_EQUAL_UINT32(180000, *s.heldRemainingMs);
  TEST_ASSERT_FALSE(e.link.advertising);   // otherwise the computer reconnects at once
  e.clock.now = 180000;
  e.bt.tick();
  TEST_ASSERT_FALSE(e.bt.status().held);
  TEST_ASSERT_TRUE(e.link.advertising);
}

void test_a_disconnect_during_a_pairing_window_is_not_a_request_to_hide() {
  Env e({kA});
  e.bt.startPairing(kDefaultPairingSec);
  e.link.peerRequestedDisconnect = true;   // the handshake can end links on its own
  e.bt.tick();
  TEST_ASSERT_FALSE(e.bt.status().held);
  TEST_ASSERT_TRUE(e.link.advertising);
}

void test_a_disconnect_on_a_factory_new_board_is_not_a_request_to_hide() {
  Env e;
  e.link.peerRequestedDisconnect = true;
  e.bt.tick();
  TEST_ASSERT_FALSE(e.bt.status().held);
}

struct SteppingClock : FakeClock {  // every read of the clock moves it, like real time passing between two reads
  mutable uint32_t t = 0;
  uint32_t step = 2;
  uint32_t nowMs() const override {
    const uint32_t v = t;
    t += step;
    return v;
  }
};

void test_remaining_time_never_underflows_when_the_deadline_passes_between_two_clock_reads() {
  SteppingClock clock;
  FakeBleLink link;
  link.bonded = {kA};
  MemBlobStore blobs;
  BluetoothManager bt(clock, link, blobs);
  bt.begin();
  clock.t = 0;
  bt.disconnect();                 // hold until 180000
  clock.t = 179999;              // status(): first read 179999 (still held), second read 180001
  BluetoothStatus s = bt.status();
  TEST_ASSERT_TRUE(s.heldRemainingMs.has_value());
  TEST_ASSERT_TRUE(*s.heldRemainingMs < 1000);
  clock.t = 0;
  bt.startPairing(kMinPairingSec);  // window until 30000
  clock.t = 29999;
  s = bt.status();
  TEST_ASSERT_TRUE(!s.pairingRemainingMs.has_value() || *s.pairingRemainingMs < 1000);
}

void test_forgetting_a_computer_during_an_open_window_lets_it_close_when_that_computer_pairs_again() {
  Env e({kA, kB});
  e.bt.startPairing(kDefaultPairingSec);
  e.bt.forget(kIdA);
  e.link.bonded.push_back(kA);   // the same computer pairs again
  e.bt.tick();
  TEST_ASSERT_FALSE(e.bt.status().pairingOpen);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_run_pairing_is_open_without_timeout);
  RUN_TEST(test_with_a_bond_pairing_is_closed);
  RUN_TEST(test_pairing_window_counts_down_and_closes);
  RUN_TEST(test_pairing_window_survives_clock_wraparound);
  RUN_TEST(test_pairing_seconds_are_bounded);
  RUN_TEST(test_stop_pairing_closes_the_window);
  RUN_TEST(test_disconnect_holds_the_link_until_reconnect);
  RUN_TEST(test_starting_pairing_releases_a_hold);
  RUN_TEST(test_peers_get_stable_default_labels_and_can_be_renamed);
  RUN_TEST(test_rename_validation);
  RUN_TEST(test_forget_one_and_all);
  RUN_TEST(test_connected_computer_and_duration);
  RUN_TEST(test_connection_unknown_to_the_manager_counts_from_first_sight);
  RUN_TEST(test_unbonded_connection_is_shown_with_its_address);
  RUN_TEST(test_bluetooth_routes);
  RUN_TEST(test_status_carries_a_bluetooth_summary);
  RUN_TEST(test_change_token);
  RUN_TEST(test_device_info_in_status);
  RUN_TEST(test_disconnect_closes_an_open_pairing_window);
  RUN_TEST(test_rename_reports_a_full_storage);
  RUN_TEST(test_reading_the_status_never_writes_storage);
  RUN_TEST(test_default_names_are_persisted_by_tick_not_by_status);
  RUN_TEST(test_default_names_never_repeat_after_a_forget);
  RUN_TEST(test_forgetting_a_connected_computer_drops_its_link);
  RUN_TEST(test_forget_all_while_connected_drops_the_link_and_keeps_pairing_closed);
  RUN_TEST(test_refusal_policy);
  RUN_TEST(test_pairing_window_is_three_minutes_by_default);
  RUN_TEST(test_window_closes_by_itself_when_a_computer_pairs);
  RUN_TEST(test_status_alone_also_notices_the_new_pairing);
  RUN_TEST(test_window_stays_open_when_a_known_computer_reconnects);
  RUN_TEST(test_stopping_the_window_by_hand_still_works);
  RUN_TEST(test_disconnect_hold_lasts_three_minutes_then_the_computer_can_reconnect);
  RUN_TEST(test_hold_countdown_is_in_the_json);
  RUN_TEST(test_losing_every_bond_does_not_make_the_board_factory_new_again);
  RUN_TEST(test_the_paired_before_memory_survives_a_restart);
  RUN_TEST(test_a_board_with_stored_names_counts_as_paired_before);
  RUN_TEST(test_a_factory_new_board_stays_open_until_the_first_pairing);
  RUN_TEST(test_a_failed_save_of_the_paired_flag_is_retried);
  RUN_TEST(test_the_computer_disconnecting_hides_the_device_like_the_dashboard_does);
  RUN_TEST(test_a_disconnect_during_a_pairing_window_is_not_a_request_to_hide);
  RUN_TEST(test_a_disconnect_on_a_factory_new_board_is_not_a_request_to_hide);
  RUN_TEST(test_remaining_time_never_underflows_when_the_deadline_passes_between_two_clock_reads);
  RUN_TEST(test_forgetting_a_computer_during_an_open_window_lets_it_close_when_that_computer_pairs_again);
  return UNITY_END();
}
