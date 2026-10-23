#!/usr/bin/env python3
"""A stand-in for the board, to develop and screenshot the dashboard without hardware.

    python3 tools/mock_device.py [--port 8080] [--token mocktoken1]

Serves the dashboard built from web/ plus an in-memory imitation of the API (status, Bluetooth pairing and
peers, config, scripts, automations, devices, network, token). Not a test oracle: the firmware's host tests are.
Scenarios can be forced for screenshots:  curl -X POST localhost:8080/__scenario/<name>
  firstrun (no computer paired) | connected | idle | held | pairing | many
"""
import argparse
import json
import pathlib
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build_web  # noqa: E402

PROFILES = {
    "profiles": [
        {"id": "logitech-mx-master-3s", "label": "Logitech MX Master 3S", "kind": "mouse", "name": "MX Master 3S",
         "manufacturer": "Logitech", "vid": 1133, "pid": 45108, "oui": "00:1F:20"},
        {"id": "razer-orochi-v2", "label": "Razer Orochi V2", "kind": "mouse", "name": "Razer Orochi V2",
         "manufacturer": "Razer", "vid": 5426, "pid": 706, "oui": None},
    ],
    "custom": {"mouse": {"name": "Bluetooth Mouse", "manufacturer": "Generic", "vid": 4617, "pid": 1}},
}


class Device:
    def __init__(self, token: str):
        self.lock = threading.Lock()
        self.token = token
        self.config = {"minIntervalSec": 30, "maxIntervalSec": 120, "amplitudePx": 150,
                       "clickProbability": 0, "scrollProbability": 0.1}
        self.state = "running"
        self.moves = 128
        self.last_move = time.time() - 40
        self.next_move_at = time.time() + 47
        self.appearance = {"profile": "custom"}
        self.network = {"mode": "auto", "apSsid": "Jiggler", "apPassword": ""}
        self.stop_at = None
        self.held_until = None
        self.bonded_at_open = None
        self.tz = "Europe/London"
        self.schedules = [{"id": "workdays", "enabled": True, "days": ["mon", "tue", "wed", "thu", "fri"], "start": "09:00", "durationMin": 480}]
        self.scenario("connected")

    def scenario(self, name):
        now = time.time()
        self.held = False
        self.window_until = None
        self.connected_since = None
        self.peers = []
        self.held_until = None
        self.bonded_at_open = None
        if name == "firstrun":
            self.state = "stopped"
            self.stop_at = None
        elif name in ("connected", "held", "pairing", "idle", "many"):
            self.peers = [{"address": "02:00:00:00:00:01", "label": "Computer 1"}]
            if name == "many":
                self.peers += [{"address": "02:00:00:00:00:02", "label": "Office desktop"},
                               {"address": "02:00:00:00:00:03", "label": "Computer 3"}]
            if name in ("connected", "pairing", "many"):
                self.connected_since = now - 12 * 60 - 20
            if name == "held":
                self.held = True
                self.held_until = now + 140
            if name == "pairing":
                self.window_until = now + 95
        else:
            return False
        if name != "firstrun":
            self.state = "running"
        return True

    def pair_computer(self):
        """Simulate a computer finishing pairing while the window is open (only then)."""
        self.expire()
        if not self.window_until:
            return False
        n = len(self.peers) + 1
        self.peers.append({"address": "02:00:00:00:00:%02x" % (0x10 + n), "label": "Computer %d" % n})
        self.connected_since = time.time()
        self.window_until = None
        return True

    def expire(self):
        now = time.time()
        if self.window_until and (now >= self.window_until or (self.bonded_at_open is not None and len(self.peers) > self.bonded_at_open)):
            self.window_until = None
        if self.held and self.held_until and now >= self.held_until:
            self.held = False
            self.held_until = None
        if self.state == "running" and self.stop_at and now >= self.stop_at:
            self.state = "stopped"
            self.stop_at = None

    def appearance_json(self, restart):
        a = dict(self.appearance)
        prof = next((p for p in PROFILES["profiles"] if p["id"] == a["profile"]), None)
        out = dict(a)
        out["shownAs"] = a.get("name") or (prof["name"] if prof else PROFILES["custom"]["mouse"]["name"])
        out["profiles"] = PROFILES["profiles"]
        out["custom"] = PROFILES["custom"]["mouse"]
        if restart:
            out["restartRequired"] = True
        return out

    def pid(self, address):
        return address.replace(":", "")

    def connected_addr(self):
        return self.peers[0]["address"] if (self.connected_since and not self.held and self.peers) else None

    def bluetooth(self):
        now = time.time()
        self.expire()
        open_ = bool(self.window_until)
        connected = self.connected_addr() is not None
        out = {"connected": connected, "held": self.held,
               "heldRemainingMs": int((self.held_until - now) * 1000) if self.held and self.held_until else None,
               "pairing": {"open": open_, "remainingMs": int((self.window_until - now) * 1000) if self.window_until else None, },
               "computer": None, "peers": []}
        if connected:
            out["computer"] = {"label": self.peers[0]["label"], "connectedForMs": int((now - self.connected_since) * 1000)}
        for p in self.peers:
            out["peers"].append({"id": self.pid(p["address"]), "address": p["address"], "label": p["label"],
                                 "connected": connected and p is self.peers[0]})
        return out

    def status(self):
        now = time.time()
        bt = self.bluetooth()
        summary = {k: bt[k] for k in ("connected", "held", "heldRemainingMs", "pairing", "computer")}
        summary["pairing"] = {"open": bt["pairing"]["open"], "remainingMs": bt["pairing"]["remainingMs"]}
        summary["peerCount"] = len(bt["peers"])
        if self.state == "running" and now >= self.next_move_at:
            self.moves += 1
            self.last_move = now
            self.next_move_at = now + 30 + (self.moves % 7) * 5
        return {"state": self.state, "mouseConnected": bt["connected"], "wifiConnected": True,
                "msToNextMove": int((self.next_move_at - now) * 1000) if self.state == "running" else None,
                "stopsInMs": int((self.stop_at - now) * 1000) if self.state == "running" and self.stop_at else None,
                "schedule": {"timeSynced": True, "localTime": time.strftime("%H:%M"), "weekday": ["mon", "tue", "wed", "thu", "fri", "sat", "sun"][time.localtime().tm_wday],
                             "active": False, "enabledCount": sum(1 for s in self.schedules if s["enabled"]), "timezone": self.tz},
                "moveCount": self.moves, "lastMoveAgoMs": int((now - self.last_move) * 1000), "lastMoveAt": int(self.last_move),
                "config": self.config, "bluetooth": summary,
                "device": {"version": "0.4.0", "bleName": "Bluetooth Mouse", "bleAddress": "02:aa:bb:cc:00:01"}}


DEV = None
PAGE = None


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def send(self, status, body, ctype="application/json", extra=None):
        data = body if isinstance(body, bytes) else (json.dumps(body) if not isinstance(body, str) else body).encode()
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n).decode() if n else ""
        try:
            return json.loads(raw) if raw else {}
        except ValueError:
            return None

    def err(self, status, code, field="", message=""):
        e = {"error": code}
        if field:
            e["field"] = field
        if message:
            e["message"] = message
        self.send(status, e)

    def do_GET(self):
        self.route("GET")

    def do_POST(self):
        self.route("POST")

    def do_PUT(self):
        self.route("PUT")

    def do_DELETE(self):
        self.route("DELETE")

    def route(self, method):
        path = self.path.split("?")[0]
        if method == "GET" and path == "/":
            return self.send(200, PAGE, "text/html")
        if method == "GET" and path == "/manifest.webmanifest":
            return self.send(200, (ROOT / "web" / "manifest.webmanifest").read_bytes(), "application/manifest+json")
        if method == "GET" and path in ("/icon-192.png", "/icon-512.png", "/apple-touch-icon.png"):
            return self.send(200, (ROOT / "web" / "icons" / path[1:]).read_bytes(), "image/png")
        if method == "POST" and path == "/__pair":
            with DEV.lock:
                ok = DEV.pair_computer()
            return self.send(200 if ok else 409, {"ok": ok})
        if method == "POST" and path.startswith("/__scenario/"):
            with DEV.lock:
                ok = DEV.scenario(path.rsplit("/", 1)[1])
            return self.send(200 if ok else 404, {"ok": ok})
        if not path.startswith("/api/"):
            return self.err(404, "not_found")
        if self.headers.get("Authorization") != "Bearer " + DEV.token:
            return self.send(401, {"error": "unauthorized"})
        body = self.body() if method in ("POST", "PUT") else {}
        with DEV.lock:
            self.api(method, path, body)

    def api(self, method, path, body):
        d = DEV
        if path == "/api/status" and method == "GET":
            return self.send(200, d.status())
        if path == "/api/start":
            minutes = (body or {}).get("minutes") if isinstance(body, dict) else None
            if minutes is not None and (not isinstance(minutes, int) or not 1 <= minutes <= 1440):
                return self.err(400, "invalid_minutes", "minutes", "must be between 1 and 1440")
            if not d.peers:
                return self.err(409, "no_computer_paired", "", "pair a computer first: there is nothing to keep awake yet")
            d.state = "running"; d.next_move_at = time.time() + 30
            d.stop_at = time.time() + minutes * 60 if minutes else None
            return self.send(200, d.status())
        if path == "/api/pause":
            d.state = "paused"; d.stop_at = None
            return self.send(200, d.status())
        if path == "/api/stop":
            d.state = "stopped"; d.stop_at = None
            return self.send(200, d.status())
        if path == "/api/move":
            if not d.connected_addr():
                return self.err(409, "mouse_not_connected")
            d.moves += 1; d.last_move = time.time()
            return self.send(202, {"ok": True})
        if path == "/api/config":
            if method == "PUT":
                for k, v in (body or {}).items():
                    if k in ("minIntervalSec", "maxIntervalSec") and not 5 <= v <= 3600:
                        return self.err(400, "invalid_config", k, "must be between 5 and 3600")
                    d.config[k] = v
            return self.send(200, d.config)
        if path == "/api/restart":
            return self.send(202, {"restarting": True})
        if path == "/api/appearance":
            if method == "PUT":
                b = body or {}
                names = [p["id"] for p in PROFILES["profiles"]] + ["custom"]
                if b.get("profile", "custom") not in names:
                    return self.err(400, "invalid_appearance", "profile", "unknown profile")
                d.appearance = {k: v for k, v in b.items() if k in ("profile", "name", "manufacturer", "vid", "pid", "mac")}
                d.appearance.setdefault("profile", "custom")
                return self.send(200, d.appearance_json(True))
            return self.send(200, d.appearance_json(False))
        if path == "/api/network":
            if method == "PUT":
                d.network.update(body)
            return self.send(200, d.network)
        if path == "/api/schedules":
            zones = ["UTC", "Europe/London", "Europe/Rome", "Europe/Paris", "Europe/Berlin", "America/New_York", "America/Los_Angeles", "Asia/Tokyo"]
            if method == "PUT":
                b = body or {}
                if "timezone" in b:
                    d.tz = b["timezone"]
                for i, s in enumerate(b.get("schedules", d.schedules)):
                    if not s.get("days"):
                        return self.err(400, "invalid_schedule", "schedules[%d].days" % i, "pick at least one day")
                d.schedules = b.get("schedules", d.schedules)
            return self.send(200, {"timezone": d.tz, "schedules": d.schedules, "timezones": zones})
        if path == "/api/token" and method == "PUT":
            if body.get("current") != d.token:
                return self.err(403, "wrong_current", "current", "the current token is not correct")
            if not 8 <= len(body.get("token", "")) <= 40:
                return self.err(400, "invalid_token", "token", "must be 8 to 40 printable characters without spaces")
            d.token = body["token"]
            return self.send(200, {"ok": True})
        if path == "/api/bluetooth" and method == "GET":
            return self.send(200, d.bluetooth())
        if path == "/api/bluetooth/pairing":
            if method == "DELETE":
                d.window_until = None
                return self.send(200, d.bluetooth())
            seconds = (body or {}).get("seconds", 180)
            if not 30 <= seconds <= 600:
                return self.err(400, "invalid_seconds", "seconds", "the pairing window must last between 30 and 600 seconds")
            d.window_until = time.time() + seconds; d.held = False; d.bonded_at_open = len(d.peers)
            return self.send(200, d.bluetooth())
        if path == "/api/bluetooth/disconnect":
            d.held = True; d.held_until = time.time() + 180; d.window_until = None
            return self.send(200, d.bluetooth())
        if path == "/api/bluetooth/reconnect":
            d.held = False
            if d.peers and not d.connected_since:
                d.connected_since = time.time()
            return self.send(200, d.bluetooth())
        if path == "/api/bluetooth/peers" and method == "DELETE":
            d.peers = []; d.connected_since = None; d.held = False
            return self.send(200, d.bluetooth())
        if path.startswith("/api/bluetooth/peers/"):
            pid = path.rsplit("/", 1)[1]
            match = [p for p in d.peers if d.pid(p["address"]) == pid]
            if not match:
                return self.err(404, "not_found", "", "no such paired computer")
            if method == "DELETE":
                d.peers.remove(match[0])
                if not d.peers:
                    d.connected_since = None
                return self.send(200, d.bluetooth())
            label = (body or {}).get("label", "").strip()
            if not 1 <= len(label) <= 32:
                return self.err(400, "invalid_label", "label", "the name must be 1 to 32 characters")
            match[0]["label"] = label
            return self.send(200, d.bluetooth())
        return self.err(404, "not_found")


def main():
    global DEV, PAGE
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--token", default="mocktoken1")
    args = ap.parse_args()
    DEV = Device(args.token)
    PAGE = build_web.build().split('R"HTML(', 1)[1].rsplit(')HTML"', 1)[0]
    print(f"mock device on http://localhost:{args.port}  token: {args.token}")
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
