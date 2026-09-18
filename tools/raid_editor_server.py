#!/usr/bin/env python3
"""raid_editor_server.py - the RE1 editor's local backend.

CUSTOM. Not part of the original game.

SUPERSEDED. The editor is in the game now (RAID mode, F2 - see
docs/RE1_EDITOR.md), and raid_editor.html has been deleted from the tree, so
this serves a 404 and nothing else. Kept for the notes below.

WHY THIS EXISTED

tools/raid_editor.html is a browser page, and a browser page cannot start a
program. Unreal's Play button is the one thing an editor really cannot fake, so
the page needs something on this machine that can: this file.

It is also what turns the editor from "a page you point at a folder" into an
application. Served from here, the page can read the level and every model file
over HTTP with no folder picker, no permission prompt and no case-sensitivity
trouble - and it can ask for the game to be launched, and be told when it exits.

Standard library only, no install, no dependencies. It binds to 127.0.0.1, so
nothing outside this machine can reach it.

    py tools\\raid_editor_server.py            # then open the printed URL
    py tools\\raid_editor_server.py --port 9000 --config Debug

or just double-click tools\\raid_editor.bat.
"""

import argparse
import http.server
import json
import mimetypes
import os
import socketserver
import subprocess
import sys
import threading
import time
import urllib.parse
import webbrowser

# ---------------------------------------------------------------------------
# Where things are. The script lives in tools/, so the repository is its parent.
# ---------------------------------------------------------------------------
TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.dirname(TOOLS_DIR)

EDITOR_PAGE = os.path.join(TOOLS_DIR, "raid_editor.html")
LEVEL_NAME = "raid1.lvl"

# The level is mirrored into all three trees, because the exe reads the copy
# beside whichever build it was launched from.
LEVEL_DIRS = [
    os.path.join(REPO_DIR, "assets", "USA", "Data"),
    os.path.join(REPO_DIR, "bin", "Debug", "USA", "Data"),
    os.path.join(REPO_DIR, "bin", "Release", "USA", "Data"),
]

# Asset reads are confined to these, by name. A path that is not under one of
# them is refused - the page is local and trusted, but a server that will read
# any file on the disk because a URL said so is a bad habit whatever its reason.
ASSET_ROOTS = {
    "Item_m2": os.path.join(REPO_DIR, "assets", "USA", "Item_m2"),
    "Enemy":   os.path.join(REPO_DIR, "assets", "USA", "Enemy"),
    "Players": os.path.join(REPO_DIR, "assets", "USA", "Players"),
    "Data":    os.path.join(REPO_DIR, "assets", "USA", "Data"),
}

EXE_NAME = "residentevil.exe"


class GameProcess(object):
    """The running game, if there is one.

    Play and Stop are the whole contract. Launching twice replaces the first,
    which is what pressing Play again means.
    """

    def __init__(self):
        self.proc = None
        self.config = None
        self.started = 0.0
        self.lock = threading.Lock()

    def exe_path(self, config):
        return os.path.join(REPO_DIR, "bin", config, EXE_NAME)

    def play(self, config):
        exe = self.exe_path(config)
        if not os.path.isfile(exe):
            return False, "no %s - build the %s configuration first" % (
                os.path.relpath(exe, REPO_DIR), config)
        with self.lock:
            self._stop_locked()
            try:
                # cwd is the build directory: the exe resolves its own USA\
                # tree relative to where it is started, so launching it from
                # anywhere else finds no data at all.
                self.proc = subprocess.Popen([exe], cwd=os.path.dirname(exe))
            except OSError as e:
                return False, str(e)
            self.config = config
            self.started = time.time()
        return True, "started %s (pid %d)" % (config, self.proc.pid)

    def _stop_locked(self):
        if self.proc is None:
            return
        if self.proc.poll() is None:
            try:
                self.proc.terminate()
                try:
                    self.proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
            except OSError:
                pass
        self.proc = None

    def stop(self):
        with self.lock:
            running = self.proc is not None and self.proc.poll() is None
            self._stop_locked()
        return running

    def status(self):
        with self.lock:
            if self.proc is None:
                return {"running": False}
            code = self.proc.poll()
            if code is None:
                return {"running": True, "pid": self.proc.pid,
                        "config": self.config,
                        "seconds": int(time.time() - self.started)}
            proc, self.proc = self.proc, None
            return {"running": False, "exitCode": code, "config": self.config}


GAME = GameProcess()


def level_path():
    """The level the editor reads. The assets tree is the source of truth; the
    two build trees are copies it is saved into."""
    return os.path.join(LEVEL_DIRS[0], LEVEL_NAME)


def read_level():
    p = level_path()
    if not os.path.isfile(p):
        return None
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def write_level(text):
    written = []
    for d in LEVEL_DIRS:
        if not os.path.isdir(d):
            continue
        p = os.path.join(d, LEVEL_NAME)
        try:
            with open(p, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            written.append(os.path.relpath(p, REPO_DIR))
        except OSError:
            pass
    return written


def resolve_asset(rel):
    """A repo-relative asset path, resolved case-insensitively inside one of the
    allowed roots. The real tree has I26V.IVM next to IACD.ivm, so an exact
    match is not something a caller can be asked to produce."""
    rel = rel.replace("\\", "/").strip("/")
    parts = [p for p in rel.split("/") if p not in ("", ".", "..")]
    if len(parts) != 2:
        return None
    root = ASSET_ROOTS.get(parts[0]) or ASSET_ROOTS.get(parts[0].capitalize())
    if root is None:
        for k, v in ASSET_ROOTS.items():
            if k.lower() == parts[0].lower():
                root = v
                break
    if root is None or not os.path.isdir(root):
        return None
    want = parts[1].lower()
    for name in os.listdir(root):
        if name.lower() == want:
            return os.path.join(root, name)
    return None


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "RE1Editor/1.0"

    # The default logs every request to stderr, which drowns the one line that
    # matters (the URL to open).
    def log_message(self, fmt, *args):
        pass

    # ---- helpers ----------------------------------------------------------
    def _send(self, code, body, ctype="application/octet-stream", extra=None):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        # The page is same-origin when served from here, but a file:// copy is
        # still a supported way to run it, and that one is a null origin.
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _json(self, obj, code=200):
        self._send(code, json.dumps(obj), "application/json; charset=utf-8")

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    # ---- routes -----------------------------------------------------------
    def do_OPTIONS(self):
        self._send(204, b"", "text/plain", {
            "Access-Control-Allow-Methods": "GET, PUT, POST, OPTIONS",
            "Access-Control-Allow-Headers": "Content-Type",
        })

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)
        path = u.path

        if path in ("/", "/index.html", "/raid_editor.html"):
            if not os.path.isfile(EDITOR_PAGE):
                return self._send(404, "raid_editor.html is missing from tools/",
                                  "text/plain; charset=utf-8")
            with open(EDITOR_PAGE, "rb") as f:
                return self._send(200, f.read(), "text/html; charset=utf-8")

        if path == "/api/status":
            st = GAME.status()
            lv = level_path()
            st["level"] = os.path.relpath(lv, REPO_DIR)
            st["levelExists"] = os.path.isfile(lv)
            st["levelMtime"] = int(os.path.getmtime(lv) * 1000) if os.path.isfile(lv) else 0
            st["repo"] = REPO_DIR
            st["builds"] = [c for c in ("Debug", "Release")
                            if os.path.isfile(GAME.exe_path(c))]
            return self._json(st)

        if path == "/api/level":
            text = read_level()
            if text is None:
                return self._send(404, "no level yet", "text/plain; charset=utf-8")
            return self._send(200, text, "text/plain; charset=utf-8")

        if path == "/api/assets":
            d = (q.get("dir") or [""])[0]
            root = ASSET_ROOTS.get(d)
            if root is None:
                for k, v in ASSET_ROOTS.items():
                    if k.lower() == d.lower():
                        root = v
                        break
            if root is None or not os.path.isdir(root):
                return self._json({"files": []})
            files = sorted(n for n in os.listdir(root)
                           if os.path.isfile(os.path.join(root, n)))
            return self._json({"dir": d, "files": files})

        if path == "/api/asset":
            rel = (q.get("path") or [""])[0]
            p = resolve_asset(rel)
            if p is None or not os.path.isfile(p):
                return self._send(404, "no such asset", "text/plain; charset=utf-8")
            with open(p, "rb") as f:
                return self._send(200, f.read(), "application/octet-stream")

        # Anything else in tools/ - a stylesheet or a script, if the page ever
        # grows into more than one file.
        name = os.path.basename(path)
        cand = os.path.join(TOOLS_DIR, name)
        if name and os.path.isfile(cand):
            ctype = mimetypes.guess_type(cand)[0] or "application/octet-stream"
            with open(cand, "rb") as f:
                return self._send(200, f.read(), ctype)

        self._send(404, "not found", "text/plain; charset=utf-8")

    def do_PUT(self):
        if urllib.parse.urlparse(self.path).path != "/api/level":
            return self._send(404, "not found", "text/plain; charset=utf-8")
        text = self._body().decode("utf-8", "replace")
        if len(text.strip()) < 8:
            return self._json({"ok": False, "error": "refusing to write an empty level"}, 400)
        written = write_level(text)
        return self._json({"ok": bool(written), "written": written})

    def do_POST(self):
        path = urllib.parse.urlparse(self.path).path
        if path == "/api/play":
            try:
                req = json.loads(self._body() or b"{}")
            except ValueError:
                req = {}
            config = req.get("config") or "Debug"
            if config not in ("Debug", "Release"):
                config = "Debug"
            ok, msg = GAME.play(config)
            return self._json({"ok": ok, "message": msg, "config": config})
        if path == "/api/stop":
            was = GAME.stop()
            return self._json({"ok": True, "wasRunning": was})
        self._send(404, "not found", "text/plain; charset=utf-8")


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    ap = argparse.ArgumentParser(description="Local backend for the RE1 level editor.")
    ap.add_argument("--port", type=int, default=8787)
    ap.add_argument("--config", default="Debug", choices=("Debug", "Release"),
                    help="which build the Play button launches by default")
    ap.add_argument("--no-browser", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(EDITOR_PAGE):
        print("error: %s not found" % EDITOR_PAGE)
        return 1

    url = "http://127.0.0.1:%d/?config=%s" % (args.port, args.config)
    try:
        srv = Server(("127.0.0.1", args.port), Handler)
    except OSError as e:
        print("error: cannot listen on port %d (%s)" % (args.port, e))
        print("       another copy may already be running - try --port %d" % (args.port + 1))
        return 1

    print("RE1 editor")
    print("  repo    %s" % REPO_DIR)
    print("  level   %s" % os.path.relpath(level_path(), REPO_DIR))
    print("  open    %s" % url)
    print("  stop    Ctrl+C")
    if not args.no_browser:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping")
    finally:
        GAME.stop()
        srv.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
