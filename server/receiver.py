#!/usr/bin/env python3
"""AirWatch log receiver.

The board POSTs its mark log and alert journal here when it joins the home
Wi-Fi. Each upload is saved as its own file, and appended to a running
all-marks / all-alerts file. The reply "OK <bytes> <crc32>" lets the board
check that the copy is exact before it clears its own storage.

Standard library only. Settings come from environment variables:
  AIRWATCH_TOKEN  shared secret; must match UPLOAD_TOKEN in secrets.h
  AIRWATCH_DATA   where files go (default /data)
  AIRWATCH_PORT   port to listen on (default 8077)
"""
import datetime
import html
import hmac
import os
import re
import urllib.parse
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

TOKEN = os.environ.get("AIRWATCH_TOKEN", "")
DATA = os.environ.get("AIRWATCH_DATA", "/data")
PORT = int(os.environ.get("AIRWATCH_PORT", "8077"))
MAX_BYTES = 16 * 1024 * 1024
KINDS = {"marks", "alerts"}


def log(msg):
    print(f"{datetime.datetime.now().isoformat(timespec='seconds')} {msg}", flush=True)


class Handler(BaseHTTPRequestHandler):
    server_version = "AirWatchReceiver/1"

    def log_message(self, fmt, *args):  # quieter default logging
        pass

    def reply(self, code, body, ctype="text/plain; charset=utf-8"):
        data = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    # ---- upload from the board ----
    def do_POST(self):
        url = urllib.parse.urlparse(self.path)
        if url.path != "/upload":
            return self.reply(404, "not found")
        if not TOKEN or not hmac.compare_digest(self.headers.get("X-AirWatch-Token", ""), TOKEN):
            log(f"refused upload from {self.client_address[0]}: bad token")
            return self.reply(403, "bad token")
        q = urllib.parse.parse_qs(url.query)
        kind = q.get("kind", [""])[0]
        drive = re.sub(r"[^0-9]", "", q.get("drive", ["0"])[0])[:10] or "0"
        if kind not in KINDS:
            return self.reply(400, "bad kind")
        try:
            length = int(self.headers.get("Content-Length", "-1"))
        except ValueError:
            length = -1
        if length < 0 or length > MAX_BYTES:
            return self.reply(411 if length < 0 else 413, "bad length")

        body = bytearray()
        while len(body) < length:
            chunk = self.rfile.read(min(65536, length - len(body)))
            if not chunk:
                break
            body.extend(chunk)
        if len(body) != length:
            log(f"short upload from {self.client_address[0]}: {len(body)} of {length} bytes")
            return self.reply(400, "short body")

        now = datetime.datetime.now()
        day = os.path.join(DATA, now.strftime("%Y-%m-%d"))
        os.makedirs(day, exist_ok=True)
        name = f"{now.strftime('%H%M%S')}_drive{drive}_{kind}.log"
        path = os.path.join(day, name)
        n = 1
        while os.path.exists(path):  # two uploads in the same second
            n += 1
            path = os.path.join(day, f"{now.strftime('%H%M%S')}_drive{drive}_{kind}_{n}.log")
        with open(path, "wb") as f:
            f.write(body)
            f.flush()
            os.fsync(f.fileno())
        with open(os.path.join(DATA, f"all-{kind}.log"), "ab") as f:
            f.write(body)
            if body and not body.endswith(b"\n"):
                f.write(b"\n")
            f.flush()
            os.fsync(f.fileno())

        crc = zlib.crc32(body) & 0xFFFFFFFF
        log(f"saved {path} ({length} bytes, crc {crc:08x}) from {self.client_address[0]}")
        self.reply(200, f"OK {length} {crc:08x}")

    # ---- browsing the saved files ----
    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        rel = urllib.parse.unquote(url.path).lstrip("/")
        if rel in ("", "index.html"):
            return self.reply(200, self.index(), "text/html; charset=utf-8")
        full = os.path.realpath(os.path.join(DATA, rel))
        if not full.startswith(os.path.realpath(DATA) + os.sep) or not os.path.isfile(full):
            return self.reply(404, "not found")
        with open(full, "rb") as f:
            data = f.read()
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        if "dl" in urllib.parse.parse_qs(url.query):
            self.send_header("Content-Disposition", f'attachment; filename="{os.path.basename(full)}"')
        self.end_headers()
        self.wfile.write(data)

    def index(self):
        rows = []
        for top in ("all-marks.log", "all-alerts.log"):
            p = os.path.join(DATA, top)
            if os.path.isfile(p):
                rows.append(("", top, os.path.getsize(p)))
        days = sorted((d for d in os.listdir(DATA) if os.path.isdir(os.path.join(DATA, d))), reverse=True)
        for d in days:
            for fn in sorted(os.listdir(os.path.join(DATA, d)), reverse=True):
                rows.append((d, f"{d}/{fn}", os.path.getsize(os.path.join(DATA, d, fn))))
        items = "".join(
            f'<tr><td>{html.escape(day)}</td><td><a href="/{urllib.parse.quote(rel)}">{html.escape(rel)}</a></td>'
            f'<td>{size:,} B</td><td><a href="/{urllib.parse.quote(rel)}?dl=1">download</a></td></tr>'
            for day, rel, size in rows)
        return ("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
                "<title>AirWatch logs</title><style>body{font:15px system-ui;margin:16px;background:#111;color:#ddd}"
                "a{color:#7cf}td{padding:4px 10px;border-bottom:1px solid #333}</style>"
                f"<h2>AirWatch logs</h2><table>{items or '<tr><td>No uploads yet.</td></tr>'}</table>")


if __name__ == "__main__":
    if not TOKEN:
        raise SystemExit("Set AIRWATCH_TOKEN (the same value as UPLOAD_TOKEN in secrets.h).")
    os.makedirs(DATA, exist_ok=True)
    log(f"listening on port {PORT}, saving to {DATA}")
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
