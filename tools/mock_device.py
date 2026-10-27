#!/usr/bin/env python3
"""A stand-in for the board, to develop and screenshot the dashboard without hardware.

    python3 tools/mock_device.py [--port 8080] [--token mocktoken1]

Serves the dashboard built from web/ plus an in-memory imitation of the API (status, Bluetooth pairing and
peers, config, scripts, automations, devices, network, token). Not a test oracle: the firmware's host tests are.
Scenarios can be forced for screenshots:  curl -X POST localhost:8080/__scenario/<name>
  firstrun (no computer paired) | connected | idle | off | pairing | many
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
        {"id": "logitech-mx-master-3", "label": "Logitech MX Master 3", "kind": "mouse", "name": "MX Master 3",
         "manufacturer": "Logitech", "vid": 1133, "pid": 45091, "oui": "00:1F:20"},
        {"id": "logitech-mx-anywhere-3", "label": "Logitech MX Anywhere 3", "kind": "mouse", "name": "MX Anywhere 3",
         "manufacturer": "Logitech", "vid": 1133, "pid": 45093, "oui": "00:1F:20"},
        {"id": "razer-orochi-v2", "label": "Razer Orochi V2", "kind": "mouse", "name": "Razer Orochi V2",
         "manufacturer": "Razer", "vid": 5426, "pid": 149, "oui": "44:5E:CD"},
        {"id": "apple-magic-mouse-2", "label": "Apple Magic Mouse 2", "kind": "mouse", "name": "Magic Mouse 2",
         "manufacturer": "Apple Inc.", "vid": 76, "pid": 617, "oui": "58:AD:12"},
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
        self.resume_at = None
        self.bonded_at_open = None
        self.tz = "Europe/London"
        self.ntp = "pool.ntp.org"
        self.time_source = "ntp"
        self.disc_seconds = 180
        self.off_on_disconnect = True
        self.button_gpio = 0
        self.window_open = False
        self.owner_stopped = False
        self.upd = {"mode": "manual", "checkTime": "03:00", "repoUrl": "https://github.com/carlosatta/jiggler", "latest": None, "checked_at": None,
                    "busy_until": None, "kind": None, "error": None}
        self.schedules = [{"id": "workdays", "enabled": True, "kind": "window", "days": ["mon", "tue", "wed", "thu", "fri"], "start": "09:00",
                           "durationMin": 480, "jitterMin": 10, "times": None, "remaining": None},
                          {"id": "lunch", "enabled": True, "kind": "action", "action": "pause", "days": ["mon", "tue", "wed", "thu", "fri"],
                           "start": "12:30", "jitterMin": 5, "times": None, "remaining": None}]
        self.scenario("connected")

    def scenario(self, name):
        now = time.time()
        self.enabled = True
        self.window_until = None
        self.connected_since = None
        self.peers = []
        self.bonded_at_open = None
        self.window_open = name == "windowstopped"
        self.owner_stopped = name == "windowstopped"
        if name == "windowstopped":
            self.peers = [{"address": "02:00:00:00:00:01", "label": "Computer 1"}]
            self.connected_since = now - 600
            self.state = "stopped"
            return True
        if name == "firstrun":
            self.state = "stopped"
            self.stop_at = None
        elif name in ("connected", "off", "pairing", "idle", "many"):
            self.peers = [{"address": "02:00:00:00:00:01", "label": "Computer 1"}]
            if name == "many":
                self.peers += [{"address": "02:00:00:00:00:02", "label": "Office desktop"},
                               {"address": "02:00:00:00:00:03", "label": "Computer 3"}]
            if name in ("connected", "pairing", "many"):
                self.connected_since = now - 12 * 60 - 20
            if name == "off":
                self.enabled = False
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
        if self.state in ("running", "paused") and self.stop_at and now >= self.stop_at:
            self.state = "stopped"
            self.stop_at = None
            self.resume_at = None
        if self.state == "paused" and self.resume_at and now >= self.resume_at:
            self.state = "running"
            self.resume_at = None

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
        return self.peers[0]["address"] if (self.connected_since and self.enabled and self.peers) else None

    def bluetooth(self):
        now = time.time()
        self.expire()
        open_ = bool(self.window_until) and self.enabled
        connected = self.connected_addr() is not None
        state = "off" if not self.enabled else "discovery" if open_ else "connected" if connected else "waiting"
        out = {"state": state, "enabled": self.enabled, "discoverySeconds": self.disc_seconds,
               "offWhenComputerDisconnects": self.off_on_disconnect, "connected": connected,
               "pairing": {"open": open_, "remainingMs": int((self.window_until - now) * 1000) if open_ else None},
               "computer": None, "peers": []}
        if connected:
            out["computer"] = {"label": self.peers[0]["label"], "connectedForMs": int((now - self.connected_since) * 1000)}
        for p in self.peers:
            out["peers"].append({"id": self.pid(p["address"]), "address": p["address"], "label": p["label"],
                                 "connected": connected and p is self.peers[0]})
        return out

    def update_json(self):
        u = self.upd
        now = time.time()
        if u["busy_until"] and now >= u["busy_until"]:
            if u["kind"] == "check":
                u["latest"] = {"version": "0.7.0", "tag": "v0.7.0", "pageUrl": u["repoUrl"] + "/releases/tag/v0.7.0", "sizeBytes": 1520000}
                u["checked_at"] = now
            u["busy_until"] = None
            if u["kind"] == "install":
                u["kind"] = "restarting"
            elif u["kind"] == "check":
                u["kind"] = None
        state = "checking" if u["kind"] == "check" else "installing" if u["kind"] == "install" else "restarting" if u["kind"] == "restarting" else "idle"
        progress = 0
        if state == "installing" and u["busy_until"]:
            progress = max(0, min(99, int(100 - (u["busy_until"] - now) / 4.0 * 100)))
        elif state == "restarting":
            progress = 100
        return {"mode": u["mode"], "checkTime": u["checkTime"], "repoUrl": u["repoUrl"], "defaultRepoUrl": "https://github.com/carlosatta/jiggler", "current": "0.6.0",
                "state": state, "progress": progress, "checkedAgoMs": int((now - u["checked_at"]) * 1000) if u["checked_at"] else None,
                "latest": u["latest"], "available": bool(u["latest"]) and u["latest"]["version"] != "0.6.0", "blocked": False, "error": u["error"]}

    def time_json(self):
        wd = ["mon", "tue", "wed", "thu", "fri", "sat", "sun"][time.localtime().tm_wday]
        return {"synced": self.time_source != "none", "source": self.time_source, "timezone": self.tz,
                "localTime": time.strftime("%H:%M"), "weekday": wd, "epoch": int(time.time())}

    def status(self):
        now = time.time()
        bt = self.bluetooth()
        summary = {k: bt[k] for k in ("state", "enabled", "discoverySeconds", "offWhenComputerDisconnects", "connected", "pairing", "computer")}
        summary["peerCount"] = len(bt["peers"])
        if self.state == "running" and now >= self.next_move_at:
            self.moves += 1
            self.last_move = now
            self.next_move_at = now + 30 + (self.moves % 7) * 5
        return {"state": self.state, "mouseConnected": bt["connected"], "wifiConnected": True,
                "msToNextMove": int((self.next_move_at - now) * 1000) if self.state == "running" else None,
                "stopsInMs": int((self.stop_at - now) * 1000) if self.state in ("running", "paused") and self.stop_at else None,
                "resumesInMs": int((self.resume_at - now) * 1000) if self.state == "paused" and self.resume_at else None,
                "update": {"mode": self.upd["mode"], "available": self.update_json()["available"], "version": (self.upd["latest"] or {}).get("version"), "state": self.update_json()["state"]},
                "time": self.time_json(),
                "schedule": {"active": self.window_open, "enabledCount": sum(1 for s in self.schedules if s["enabled"]), "blocked": None, "ownerStopped": self.window_open and self.owner_stopped and self.state == "stopped"},
                "moveCount": self.moves, "lastMoveAgoMs": int((now - self.last_move) * 1000), "lastMoveAt": int(self.last_move),
                "config": self.config, "bluetooth": summary,
                "device": {"version": "0.5.0", "bleName": "Bluetooth Mouse", "bleAddress": "02:aa:bb:cc:00:01"}}


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
        if method == "POST" and path == "/api/update":
            if self.headers.get("Authorization") != "Bearer " + DEV.token:
                return self.send(401, {"error": "unauthorized"})
            self.rfile.read(int(self.headers.get("Content-Length") or 0))
            return self.send(200, {"updated": True, "restarting": True})
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
            if d.state != "running":
                return self.err(409, "not_running", "", "only a running jiggler can be paused")
            minutes = (body or {}).get("minutes") if isinstance(body, dict) else None
            d.state = "paused"
            d.resume_at = time.time() + minutes * 60 if minutes else None
            return self.send(200, d.status())
        if path == "/api/resume":
            if d.state != "paused":
                return self.err(409, "not_paused", "", "the jiggler is not paused")
            d.state = "running"; d.resume_at = None
            return self.send(200, d.status())
        if path == "/api/stop":
            d.state = "stopped"; d.stop_at = None; d.resume_at = None
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
            actions = ["start", "stop", "pause", "resume", "bluetooth_on", "bluetooth_off", "discovery"]
            if method == "PUT":
                b = body or {}
                for i, s_ in enumerate(b.get("schedules", d.schedules)):
                    if not s_.get("days"):
                        return self.err(400, "invalid_schedule", "schedules[%d].days" % i, "pick at least one day")
                d.schedules = b.get("schedules", d.schedules)
            return self.send(200, {"schedules": d.schedules, "actions": actions})
        if path == "/api/time" and method in ("GET", "PUT"):
            zones = ["UTC", "Europe/London", "Europe/Rome", "Europe/Paris", "Europe/Berlin", "America/New_York", "America/Los_Angeles", "Asia/Tokyo"]
            if method == "PUT":
                b = body or {}
                if "timezone" in b:
                    if b["timezone"] not in zones:
                        return self.err(400, "invalid_clock", "timezone", "unknown timezone")
                    d.tz = b["timezone"]
                if "ntpServer" in b:
                    d.ntp = b["ntpServer"]
            return self.send(200, {"timezone": d.tz, "ntpServer": d.ntp, "status": d.time_json(), "timezones": zones})
        if path == "/api/time/set" and method == "POST":
            d.time_source = "manual"
            return self.send(200, {"timezone": d.tz, "ntpServer": d.ntp, "status": d.time_json()})
        if path == "/api/updates" and method in ("GET", "PUT"):
            if method == "PUT":
                b = body or {}
                if "mode" in b:
                    if b["mode"] not in ("manual", "automatic", "ignore"):
                        return self.err(400, "invalid_mode", "mode", "must be manual, automatic or ignore")
                    d.upd["mode"] = b["mode"]
                if "checkTime" in b:
                    t = b["checkTime"]
                    if not (isinstance(t, str) and len(t) == 5 and t[2] == ":" and t[:2].isdigit() and t[3:].isdigit() and int(t[:2]) < 24 and int(t[3:]) < 60):
                        return self.err(400, "invalid_time", "checkTime", "must be a time of day like 03:00")
                    d.upd["checkTime"] = t
                if "repoUrl" in b:
                    url = b["repoUrl"] or "https://github.com/carlosatta/jiggler"
                    if not url.startswith("https://github.com/") or url.count("/") < 4:
                        return self.err(400, "invalid_repo_url", "repoUrl", "must be a GitHub repository address like https://github.com/owner/repo")
                    d.upd["repoUrl"] = url.rstrip("/")
                    d.upd["latest"] = None; d.upd["checked_at"] = None
            return self.send(200, d.update_json())
        if path == "/api/updates/check" and method == "POST":
            if d.update_json()["state"] != "idle":
                return self.err(409, "busy", "", "an update check or install is already running")
            d.upd["kind"] = "check"; d.upd["busy_until"] = time.time() + 2.5; d.upd["error"] = None
            return self.send(202, d.update_json())
        if path == "/api/updates/install" and method == "POST":
            if not d.update_json()["available"]:
                return self.err(409, "no_update", "", "there is no newer version to install: check for updates first")
            d.upd["kind"] = "install"; d.upd["busy_until"] = time.time() + 4
            return self.send(202, d.update_json())
        if path == "/api/button":
            gpios = [0, 4, 5, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33]
            if method == "PUT":
                g = (body or {}).get("gpio")
                if g not in gpios:
                    return self.err(400, "invalid_gpio", "gpio", "pick one of the listed pins (0 is the BOOT button)")
                d.button_gpio = g
            return self.send(200, {"gpio": d.button_gpio, "default": 0, "gpios": gpios})
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
            seconds = (body or {}).get("seconds", d.disc_seconds)
            if not 30 <= seconds <= 600:
                return self.err(400, "invalid_seconds", "seconds", "the pairing window must last between 30 and 600 seconds")
            d.window_until = time.time() + seconds; d.enabled = True; d.bonded_at_open = len(d.peers)
            return self.send(200, d.bluetooth())
        if path == "/api/bluetooth/disable":
            d.enabled = False; d.window_until = None
            return self.send(200, d.bluetooth())
        if path == "/api/bluetooth/enable":
            d.enabled = True
            if d.peers and not d.connected_since:
                d.connected_since = time.time()
            return self.send(200, d.bluetooth())
        if path == "/api/bluetooth/settings" and method == "PUT":
            b = body or {}
            if "discoverySeconds" in b:
                if not isinstance(b["discoverySeconds"], int) or not 30 <= b["discoverySeconds"] <= 600:
                    return self.err(400, "invalid_seconds", "discoverySeconds", "the discovery time must be between 30 and 600 seconds")
                d.disc_seconds = b["discoverySeconds"]
            if "offWhenComputerDisconnects" in b:
                d.off_on_disconnect = bool(b["offWhenComputerDisconnects"])
            return self.send(200, d.bluetooth())
        if path == "/api/bluetooth/peers" and method == "DELETE":
            d.peers = []; d.connected_since = None
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
