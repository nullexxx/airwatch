"""Vendor names for MAC addresses (IEEE registry) and Bluetooth company IDs.

The lists are downloaded into <data>/vendors/ and refreshed every 30 days.
Everything keeps working without them (names just show as unknown).
"""
import csv
import os
import re
import threading
import time
import urllib.request

SOURCES = {
    "oui.csv": "https://standards-oui.ieee.org/oui/oui.csv",          # MA-L, 24 bit
    "mam.csv": "https://standards-oui.ieee.org/oui28/mam.csv",        # MA-M, 28 bit
    "oui36.csv": "https://standards-oui.ieee.org/oui36/oui36.csv",    # MA-S, 36 bit
    "company_identifiers.yaml": "https://bitbucket.org/bluetooth-SIG/public/raw/main/"
                                "assigned_numbers/company_identifiers/company_identifiers.yaml",
}
MAX_AGE = 30 * 86400


class Vendors:
    def __init__(self, data_dir, log=print):
        self.dir = os.path.join(data_dir, "vendors")
        self.log = log
        self.mac = {}     # hex prefix (6, 7 or 9 chars) -> organisation
        self.cid = {}     # 0x004C -> name
        self.lock = threading.Lock()

    def refresh(self, force=False):
        os.makedirs(self.dir, exist_ok=True)
        for name, url in SOURCES.items():
            path = os.path.join(self.dir, name)
            fresh = os.path.exists(path) and time.time() - os.path.getmtime(path) < MAX_AGE
            if fresh and not force:
                continue
            try:
                req = urllib.request.Request(url, headers={"User-Agent": "airwatch-receiver"})
                with urllib.request.urlopen(req, timeout=60) as r:
                    data = r.read()
                with open(path + ".tmp", "wb") as f:
                    f.write(data)
                os.replace(path + ".tmp", path)
                self.log(f"vendors: downloaded {name} ({len(data):,} bytes)")
            except Exception as e:  # keep the old copy, if any
                self.log(f"vendors: could not download {name}: {e}")
        self.load()

    def load(self):
        mac, cid = {}, {}
        for name, width in (("oui.csv", 6), ("mam.csv", 7), ("oui36.csv", 9)):
            path = os.path.join(self.dir, name)
            if not os.path.exists(path):
                continue
            with open(path, encoding="utf-8", errors="replace", newline="") as f:
                for row in csv.reader(f):
                    if len(row) > 2 and row[0] in ("MA-L", "MA-M", "MA-S"):
                        mac[row[1].upper()[:width]] = row[2].strip()
        path = os.path.join(self.dir, "company_identifiers.yaml")
        if os.path.exists(path):
            text = open(path, encoding="utf-8", errors="replace").read()
            for m in re.finditer(r"value:\s*0x([0-9A-Fa-f]+)\s*\n\s*name:\s*'?(.*?)'?\s*$", text, re.M):
                cid[int(m.group(1), 16)] = m.group(2).replace("''", "'")
        with self.lock:
            self.mac, self.cid = mac, cid
        self.log(f"vendors: {len(mac):,} address blocks, {len(cid):,} Bluetooth company IDs")

    def for_mac(self, mac):
        """Registered organisation, '(random)' for a randomised address, or ''."""
        h = (mac or "").replace(":", "").replace("-", "").upper()
        if len(h) < 6:
            return ""
        if int(h[1], 16) & 2:
            return "(random)"
        with self.lock:
            for n in (9, 7, 6):
                v = self.mac.get(h[:n])
                if v:
                    return v
        return ""

    def for_cid(self, cid):
        try:
            n = int(str(cid), 16)
        except (TypeError, ValueError):
            return ""
        with self.lock:
            return self.cid.get(n, "")
