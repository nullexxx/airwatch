"""SQLite index of the uploaded logs, plus the analyses the web page shows.

The raw uploads under <data>/YYYY-MM-DD/ stay the source of truth. Each one is
read into airwatch.db once; delete the database and it is rebuilt from them.
"""
import contextlib
import datetime
import glob
import json
import math
import os
import re
import sqlite3
import threading
import time

import logparse

SCHEMA_VERSION = 4
SCHEMA = """
CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS files(path TEXT PRIMARY KEY, kind TEXT, size INTEGER, ingested REAL);
CREATE TABLE IF NOT EXISTS marks(
  id INTEGER PRIMARY KEY, drive INT, mark INT, saw TEXT, type TEXT, uptime INT, heard INT,
  time TEXT, lat REAL, lon REAL, mph REAL, alerting TEXT, lead TEXT, last_alert TEXT,
  followup_of INT, header TEXT, UNIQUE(drive, mark, uptime));
CREATE TABLE IF NOT EXISTS lines(
  id INTEGER PRIMARY KEY, mark_id INT, kind TEXT, mac TEXT, rssi INT, best INT, ch INT,
  ssid TEXT, name TEXT, cid TEXT, svc TEXT, wps TEXT, vie TEXT, frames TEXT, peer TEXT,
  secs INT, first_ms INT, addr TEXT, adv TEXT, alerted TEXT, tier TEXT, src TEXT, label TEXT,
  why TEXT, at_lat REAL, at_lon REAL, at_mph REAL, ago_ms INT, tl TEXT,
  from_lat REAL, from_lon REAL, to_lat REAL, to_lon REAL, probes TEXT, raw TEXT);
CREATE INDEX IF NOT EXISTS lines_mac ON lines(mac);
CREATE INDEX IF NOT EXISTS lines_mark ON lines(mark_id);
CREATE TABLE IF NOT EXISTS notes(drive INT, mark INT, saw TEXT, text TEXT, raw TEXT UNIQUE);
CREATE TABLE IF NOT EXISTS alerts(
  id INTEGER PRIMARY KEY, drive INT, up INT, time TEXT, tier TEXT, src TEXT, mac TEXT,
  best INT, ch INT, dur INT, seen INT, label TEXT, ssid TEXT, lat REAL, lon REAL, mph REAL,
  muted INT, ongoing TEXT, ended TEXT, why TEXT, tl TEXT,
  from_lat REAL, from_lon REAL, to_lat REAL, to_lon REAL, raw TEXT, UNIQUE(drive, up, mac));
CREATE INDEX IF NOT EXISTS alerts_mac ON alerts(mac);
"""

KINDS = {("POLICE", "DURING_ALERT"): "hit", ("POLICE", "NO_ALERT"): "miss",
         ("NOTHING", "DURING_ALERT"): "false", ("NOTHING", "NO_ALERT"): "none"}

# Vendors worth pointing out in the police-sighting comparison.
POLICE_HINTS = ("AXON", "TASER", "WATCHGUARD", "DIGITAL ALLY", "MOTOROLA SOLUTIONS", "GETAC",
                "PANASONIC", "CRADLEPOINT", "SIERRA WIRELESS", "PEPLINK", "PEPWAVE", "HAVIS",
                "GAMBER", "L-3", "HARRIS", "KUSTOM", "APPLIED CONCEPTS", "DECATUR", "SAFETY VISION",
                "UTILITY, INC", "ICOP", "VIEVU", "COBAN", "ZEBRA", "SKYDIO")


# Home internet gateways and extenders: never the cruiser, so ranked down.
HOME_SSID = re.compile(r"^(FIOS|VERIZON|TMOBILE|XFINITY|ATT|SPECTRUM|MYSPECTRUM|CBCI|NETGEAR\d|ORBI|"
                       r"LINKSYS|TP-LINK|DIRECT-|HP-|EERO|GOOGLE|NEST|RING|ROKU|ARLO|SONOS)", re.I)
HOME_VENDOR = ("ARCADYAN", "SERCOMM", "VANTIVA", "TECHNICOLOR", "COMMSCOPE", "ARRIS", "ASKEY",
               "ACTIONTEC", "PEGATRON", "EERO", "SONOS", "ROKU", "VIZIO", "RING LLC", "AMAZON",
               # business / store Wi-Fi: fixed to a building, not in a cruiser
               "MIST SYSTEMS", "CISCO MERAKI", "RUCKUS", "ARUBA", "HEWLETT PACKARD ENTERPRISE",
               "FORTINET", "EXTREME NETWORKS")


def iso_minus(iso, ms):
    """'2026-11-25T18:29:18Z' minus ms -> ISO string (None if unknown)"""
    if not iso or ms is None:
        return None
    try:
        t = datetime.datetime.strptime(iso, "%Y-%m-%dT%H:%M:%SZ") - datetime.timedelta(milliseconds=ms)
        return t.strftime("%Y-%m-%dT%H:%M:%SZ")
    except ValueError:
        return None


def iso_plus_s(iso, s):
    if not iso or s is None:
        return None
    return iso_minus(iso, -1000 * s)


def _decode(row):
    """tl JSON -> list"""
    if row.get("tl"):
        row["tl"] = json.loads(row["tl"])
    return row


_BT_BASE = bytes.fromhex("FB349B5F8000008000100000")


def ble_services(adv_hex):
    """16-bit service IDs in a raw advert: UUID lists, service data, and their
    32/128-bit forms when they sit on the Bluetooth base UUID."""
    out = []
    try:
        b = bytes.fromhex(adv_hex or "")
    except ValueError:
        return out
    i = 0
    while i + 1 < len(b):
        n = b[i]
        if n == 0 or i + 1 + n > len(b):
            break
        t, d = b[i + 1], b[i + 2:i + 1 + n]
        if t in (0x02, 0x03):
            out += [d[k] | d[k + 1] << 8 for k in range(0, len(d) - 1, 2)]
        elif t == 0x16 and len(d) >= 2:
            out.append(d[0] | d[1] << 8)
        elif t in (0x04, 0x05, 0x20):
            step = len(d) if t == 0x20 else 4
            for k in range(0, len(d) - 3, max(step, 4)):
                if d[k + 2] == 0 and d[k + 3] == 0:
                    out.append(d[k] | d[k + 1] << 8)
        elif t in (0x06, 0x07, 0x21):
            step = len(d) if t == 0x21 else 16
            for k in range(0, len(d) - 15, max(step, 16)):
                u = d[k:k + 16]
                if u[:12] == _BT_BASE and u[14] == 0 and u[15] == 0:
                    out.append(u[12] | u[13] << 8)
        i += 1 + n
    return sorted(set(out))


def name_pattern(name):
    """'E110-dbf' -> 'E110-###', 'RID-1581F3YT3K' -> 'RID-#': names that differ
    only in a serial number or MAC tail collapse into one pattern."""
    m = re.match(r"^(.*[-_ ])([0-9A-Za-z]{3,})$", name)
    if not m:
        return None
    tail = m.group(2)
    if not (re.search(r"\d", tail) or re.fullmatch(r"[0-9A-Fa-f]+", tail)):
        return None          # a word ("Radio"), not a serial number
    return m.group(1) + "###"


def is_random(mac):
    h = (mac or "").replace(":", "")
    return len(h) >= 2 and h[1] in "2367ABEFabef"   # locally administered bit


def dist_m(a_lat, a_lon, b_lat, b_lon):
    r = 6371000.0
    p1, p2 = math.radians(a_lat), math.radians(b_lat)
    dp, dl = p2 - p1, math.radians(b_lon - a_lon)
    h = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(min(1.0, max(0.0, h))))


class Store:
    def __init__(self, data_dir, vendors, log=print):
        self.data = data_dir
        self.path = os.path.join(data_dir, "airwatch.db")
        self.vendors = vendors
        self.log = log
        self.lock = threading.Lock()
        v = None
        if os.path.exists(self.path):
            c = sqlite3.connect(self.path)
            try:
                v = c.execute("SELECT value FROM meta WHERE key='schema'").fetchone()
            except sqlite3.OperationalError:
                pass
            finally:
                c.close()
            if v and int(v[0]) != SCHEMA_VERSION:   # older layout: rebuild from the raw files
                for suffix in ("", "-wal", "-shm"):   # WAL sidecars must go with it
                    if os.path.exists(self.path + suffix):
                        os.remove(self.path + suffix)
                log(f"index: format changed (v{v[0]} -> v{SCHEMA_VERSION}), rebuilding from the uploads")
        with self.db() as c:
            c.execute("PRAGMA journal_mode=WAL")      # readers don't wait for an upload being indexed
            c.execute("PRAGMA synchronous=NORMAL")
            c.executescript(SCHEMA)
            c.execute("INSERT OR REPLACE INTO meta VALUES('schema', ?)", (str(SCHEMA_VERSION),))

    @contextlib.contextmanager
    def db(self):
        """A connection that commits (or rolls back) and is always closed."""
        c = sqlite3.connect(self.path, timeout=30)
        c.row_factory = sqlite3.Row
        try:
            with c:
                yield c
        finally:
            c.close()

    # ---------------- ingest ----------------
    def ingest_all(self):
        n = 0
        for path in sorted(glob.glob(os.path.join(self.data, "*", "*.log"))):
            n += self.ingest(path)
        if n:
            self.log(f"index: read {n} new upload file(s)")

    def ingest(self, path):
        name = os.path.basename(path)
        kind = ("marks" if "_marks" in name else "alerts" if "_alerts" in name
                else "samples" if "_samples" in name else None)
        if not kind:
            return 0
        with self.lock, self.db() as c:
            if c.execute("SELECT 1 FROM files WHERE path=?", (path,)).fetchone():
                return 0
            with open(path, encoding="utf-8", errors="replace") as f:
                text = f.read()
            if kind in ("marks", "samples"):
                marks, notes = logparse.parse_marks(text)
                for m in marks:
                    cur = c.execute(
                        "INSERT OR IGNORE INTO marks(drive,mark,saw,type,uptime,heard,time,lat,lon,mph,"
                        "alerting,lead,last_alert,followup_of,header) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                        (m["drive"], m["mark"], m["saw"], m["type"], m["uptime"], m["heard"], m["time"],
                         m["lat"], m["lon"], m["mph"], m["alerting"], m["lead"], m["last_alert"],
                         m["followup_of"], m["header"]))
                    if cur.rowcount == 0:      # same mark uploaded twice
                        continue
                    mid = cur.lastrowid
                    c.executemany(
                        "INSERT INTO lines(mark_id,kind,mac,rssi,best,ch,ssid,name,cid,svc,wps,vie,frames,peer,"
                        "secs,first_ms,addr,adv,alerted,tier,src,label,why,at_lat,at_lon,at_mph,ago_ms,tl,"
                        "from_lat,from_lon,to_lat,to_lon,probes,raw) "
                        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                        [(mid, l["kind"], l["mac"], l["rssi"], l["best"], l["ch"], l["ssid"], l["name"],
                          l["cid"], l["svc"], l["wps"], l["vie"], l["frames"], l["peer"], l["secs"],
                          l["first_ms"], l["addr"], l["adv"], l["alerted"], l["tier"], l["src"], l["label"],
                          l["why"], l["at_lat"], l["at_lon"], l["at_mph"], l["ago_ms"],
                          json.dumps(l["tl"]) if l["tl"] else None, *l["from"], *l["to"],
                          "|".join(l["probes"]) if l["probes"] else None, l["raw"])
                         for l in m["lines"]])
                c.executemany("INSERT OR IGNORE INTO notes(drive,mark,saw,text,raw) VALUES(?,?,?,?,?)",
                              [(n["drive"], n["mark"], n["saw"], n["text"], n["raw"]) for n in notes])
            else:
                c.executemany(
                    # REPLACE: an alert journaled again (it came back, or rose to HIGH) keeps its latest line
                    "INSERT OR REPLACE INTO alerts(drive,up,time,tier,src,mac,best,ch,dur,seen,label,ssid,"
                    "lat,lon,mph,muted,ongoing,ended,why,tl,from_lat,from_lon,to_lat,to_lon,raw) "
                    "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                    [(a["drive"], a["up"], a["time"], a["tier"], a["src"], a["mac"], a["best"], a["ch"],
                      a["dur"], a["seen"], a["label"], a["ssid"], a["lat"], a["lon"], a["mph"],
                      int(a["muted"]), a["ongoing"], a["ended"], a["why"],
                      json.dumps(a["tl"]) if a["tl"] else None, *a["from"], *a["to"], a["raw"])
                     for a in logparse.parse_alerts(text)])
            c.execute("INSERT INTO files VALUES(?,?,?,?)", (path, kind, os.path.getsize(path), time.time()))
        return 1

    # ---------------- helpers ----------------
    def _saw(self, c):
        """Effective answer per (drive, mark): the latest note that set saw= wins."""
        out = {}
        for r in c.execute("SELECT drive, mark, saw FROM notes WHERE saw IS NOT NULL AND saw != '' ORDER BY rowid"):
            out[(r["drive"], r["mark"])] = r["saw"]
        return out

    def _kind(self, saw, typ):
        return KINDS.get((saw, typ), "unknown")

    def vendor(self, mac, cid=None):
        v = self.vendors.for_mac(mac)
        if cid:
            name = self.vendors.for_cid(cid)
            if name:
                v = (v + " · " if v and v != "(random)" else "") + name
        return v

    def _marks(self, c, samples=False):
        """Marks in drive order. Background samples (saw=AUTO) only when asked."""
        saw = self._saw(c)
        rows = []
        q = "SELECT * FROM marks" + ("" if samples else " WHERE saw IS NOT 'AUTO'") + " ORDER BY drive, mark"
        for r in c.execute(q):
            d = dict(r)
            d["saw_effective"] = saw.get((d["drive"], d["mark"]), d["saw"])
            d["kind"] = self._kind(d["saw_effective"], d["type"])
            rows.append(d)
        return rows

    # ---------------- pages ----------------
    def summary(self):
        with self.db() as c:
            marks = self._marks(c)
            counts = {}
            for m in marks:
                counts[m["kind"]] = counts.get(m["kind"], 0) + 1
            return {
                "marks": len(marks), "by_kind": counts,
                "alerts": c.execute("SELECT COUNT(*) FROM alerts").fetchone()[0],
                "drives": c.execute("SELECT COUNT(DISTINCT drive) FROM (SELECT drive FROM marks "
                                    "UNION SELECT drive FROM alerts)").fetchone()[0],
                "files": c.execute("SELECT COUNT(*) FROM files").fetchone()[0],
                "samples": c.execute("SELECT COUNT(*) FROM marks WHERE saw='AUTO'").fetchone()[0],
                "vendors_loaded": bool(self.vendors.mac),
            }

    def marks(self):
        with self.db() as c:
            out = []
            for m in reversed(self._marks(c)):
                alerts = [dict(l) for l in c.execute(
                    "SELECT kind, mac, tier, label, ssid, rssi, best FROM lines WHERE mark_id=? AND kind IN "
                    "('ALERT','EARLIER') ORDER BY kind, best DESC", (m["id"],))]
                m.pop("header", None)
                m["alert_lines"] = alerts
                m["notes"] = [dict(n) for n in c.execute(
                    "SELECT saw, text FROM notes WHERE drive=? AND mark=?", (m["drive"], m["mark"]))]
                out.append(m)
            return out

    def mark(self, mark_id):
        with self.db() as c:
            m = c.execute("SELECT * FROM marks WHERE id=?", (mark_id,)).fetchone()
            if not m:
                return None
            m = dict(m)
            saw = self._saw(c).get((m["drive"], m["mark"]), m["saw"])
            m["saw_effective"], m["kind"] = saw, self._kind(saw, m["type"])
            # how often each transmitter was heard in other marks: rare = interesting
            seen_elsewhere = {r["mac"]: r["n"] for r in c.execute(
                "SELECT mac, COUNT(DISTINCT mark_id) AS n FROM lines WHERE kind IN ('WIFI','BLE') "
                "AND mac IN (SELECT mac FROM lines WHERE mark_id=?) AND mark_id != ? GROUP BY mac",
                (mark_id, mark_id))}
            lines = []
            for l in c.execute("SELECT * FROM lines WHERE mark_id=? ORDER BY kind IN ('ALERT','EARLIER') DESC, "
                               "COALESCE(best, rssi) DESC", (mark_id,)):
                l = _decode(dict(l))
                l["vendor"] = self.vendor(l["mac"], l["cid"])
                l["elsewhere"] = seen_elsewhere.get(l["mac"], 0)
                # clock times from the mark's GPS time: first heard / last heard
                l["first_at"] = iso_minus(m["time"], l["first_ms"])
                l["last_at"] = iso_minus(m["time"], l["ago_ms"])
                l.pop("raw", None)
                lines.append(l)
            m["lines"] = lines
            return m

    def journal(self):
        with self.db() as c:
            rows = [_decode(dict(r)) for r in c.execute("SELECT * FROM alerts ORDER BY time DESC, drive DESC, up DESC")]
            for r in rows:
                r["vendor"] = self.vendor(r["mac"])
                r["end"] = iso_plus_s(r["time"], r["dur"])
                r.pop("raw", None)
            freq = [dict(r) for r in c.execute(
                "SELECT mac, MAX(label) AS label, MAX(ssid) AS ssid, COUNT(*) AS alerts, COUNT(DISTINCT drive) AS drives, "
                "MAX(time) AS last, MAX(best) AS best FROM alerts GROUP BY mac HAVING drives > 1 "
                "ORDER BY drives DESC, alerts DESC LIMIT 50")]
            for r in freq:
                r["vendor"] = self.vendor(r["mac"])
            return {"alerts": rows, "frequent": freq}

    def repeat(self):
        with self.db() as c:
            saw = self._saw(c)
            marks = {m["id"]: m for m in self._marks(c)}
            # devices that were alerting in two or more marks
            by_mac = {}
            for l in c.execute("SELECT mark_id, mac, label, ssid, tier FROM lines WHERE kind IN ('ALERT','EARLIER')"):
                e = by_mac.setdefault(l["mac"], {"mac": l["mac"], "label": l["label"], "ssid": l["ssid"], "marks": []})
                m = marks.get(l["mark_id"])
                if m:
                    e["marks"].append({"id": m["id"], "drive": m["drive"], "mark": m["mark"], "kind": m["kind"],
                                       "tier": l["tier"]})
            alerting = [e for e in by_mac.values() if len({x["id"] for x in e["marks"]}) > 1]
            for e in alerting:
                e["vendor"] = self.vendor(e["mac"])
            alerting.sort(key=lambda e: -len(e["marks"]))
            # any transmitter heard at police sightings in two or more different
            # places (500 m apart): the same park twice is one place, and its
            # houses would otherwise all qualify
            police_ids = {i for i, m in marks.items() if m["saw_effective"] == "POLICE"}
            heard = {}
            for l in c.execute("SELECT mark_id, mac, ssid, name, rssi FROM lines WHERE kind IN ('WIFI','BLE')"):
                m = marks.get(l["mark_id"])
                if not m:
                    continue
                e = heard.setdefault(l["mac"], {"mac": l["mac"], "ssid": l["ssid"] or l["name"], "police": [],
                                                "other": set(), "best": -127})
                if l["mark_id"] in police_ids:
                    e["police"].append(m)
                else:
                    e["other"].add(m["drive"])
                e["best"] = max(e["best"], l["rssi"] or -127)
            sightings = []
            for e in heard.values():
                places = []
                for m in e["police"]:
                    if m["lat"] is None:
                        continue
                    if all(dist_m(m["lat"], m["lon"], p[0], p[1]) > 500 for p in places):
                        places.append((m["lat"], m["lon"]))
                if len(places) >= 2 and len({m["drive"] for m in e["police"]}) >= 2:
                    sightings.append({"mac": e["mac"], "ssid": e["ssid"], "vendor": self.vendor(e["mac"]),
                                      "places": len(places),
                                      "police_drives": sorted({m["drive"] for m in e["police"]}),
                                      "other_drives": sorted(e["other"]), "best": e["best"]})
            sightings.sort(key=lambda e: (len(e["other_drives"]), -e["places"]))
            return {"alerting": alerting[:100], "sightings": sightings[:200],
                    "police_drive_count": len({marks[i]["drive"] for i in police_ids})}

    def fixed(self, radius_m=300):
        """Transmitters that alerted and keep turning up at the same spot on different drives."""
        with self.db() as c:
            alerted = {r[0] for r in c.execute("SELECT mac FROM alerts UNION SELECT mac FROM lines "
                                               "WHERE kind IN ('ALERT','EARLIER') OR alerted IS NOT NULL")}
            pos = {}   # mac -> list of (drive, lat, lon)
            for r in c.execute("SELECT drive, mac, lat, lon FROM alerts WHERE lat IS NOT NULL"):
                pos.setdefault(r["mac"], []).append((r["drive"], r["lat"], r["lon"]))
            for r in c.execute("SELECT m.drive, l.mac, COALESCE(l.at_lat, m.lat) AS lat, COALESCE(l.at_lon, m.lon) AS lon "
                               "FROM lines l JOIN marks m ON m.id = l.mark_id "
                               "WHERE COALESCE(l.at_lat, m.lat) IS NOT NULL"):
                if r["mac"] in alerted:
                    pos.setdefault(r["mac"], []).append((r["drive"], r["lat"], r["lon"]))
            info = {r["mac"]: dict(r) for r in c.execute(
                "SELECT mac, MAX(label) AS label, MAX(ssid) AS ssid, MAX(tier) AS tier FROM "
                "(SELECT mac, label, ssid, tier FROM alerts UNION ALL SELECT mac, label, ssid, tier FROM lines "
                " WHERE kind IN ('ALERT','EARLIER')) GROUP BY mac")}
        out = []
        for mac, pts in pos.items():
            if mac not in alerted:
                continue
            drives = sorted({p[0] for p in pts})
            if len(drives) < 2:
                continue
            lat = sum(p[1] for p in pts) / len(pts)
            lon = sum(p[2] for p in pts) / len(pts)
            spread = max(dist_m(lat, lon, p[1], p[2]) for p in pts)
            i = info.get(mac, {})
            out.append({"mac": mac, "label": i.get("label"), "ssid": i.get("ssid"), "tier": i.get("tier"),
                        "vendor": self.vendor(mac), "drives": drives, "lat": round(lat, 5), "lon": round(lon, 5),
                        "spread_m": round(spread), "stationary": spread <= radius_m})
        out.sort(key=lambda e: (not e["stationary"], -len(e["drives"])))
        return out

    def police(self):
        """For each police sighting: what was heard there that is rarely heard anywhere else."""
        with self.db() as c:
            marks = self._marks(c)
            total = len(marks)
            # per transmitter: distinct snapshots it was heard in, and how many of
            # those were "nothing there" marks (after corrections)
            nothing_ids = {m["id"] for m in marks if m["saw_effective"] == "NOTHING"}
            seen_in = {}
            for r in c.execute("SELECT DISTINCT mac, mark_id FROM lines WHERE kind IN ('WIFI','BLE')"):
                seen_in.setdefault(r["mac"], set()).add(r["mark_id"])
            counts = {mac: (len(ids), len(ids & nothing_ids)) for mac, ids in seen_in.items()}
            out = []
            for m in reversed(marks):
                if m["saw_effective"] != "POLICE":
                    continue
                cand = []
                for l in c.execute("SELECT * FROM lines WHERE mark_id=? AND kind IN ('WIFI','BLE')", (m["id"],)):
                    n, nothing = counts.get(l["mac"], (1, 0))
                    vendor = self.vendor(l["mac"], l["cid"])
                    hint = any(h in vendor.upper() for h in POLICE_HINTS) or bool(l["alerted"])
                    home = bool(HOME_SSID.match(l["ssid"] or "")) or any(h in vendor.upper() for h in HOME_VENDOR)
                    # rare = heard in few other marks; strong = close to you; home gear ranks down
                    score = ((3.0 if hint else 0) - (3.0 if home and not hint else 0)
                             + max(0, 4 - (n - 1)) + max(0, ((l["rssi"] or -100) + 90) / 10))
                    cand.append({"mac": l["mac"], "kind": l["kind"], "rssi": l["rssi"], "ssid": l["ssid"],
                                 "name": l["name"], "vendor": vendor, "frames": l["frames"], "peer": l["peer"],
                                 "alerted": l["alerted"], "other_marks": n - 1, "nothing_marks": nothing or 0,
                                 "hint": hint, "score": round(score, 1)})
                cand.sort(key=lambda x: -x["score"])
                out.append({"id": m["id"], "drive": m["drive"], "mark": m["mark"], "kind": m["kind"],
                            "time": m["time"], "lat": m["lat"], "lon": m["lon"], "mph": m["mph"],
                            "lead": m["lead"], "alerting": m["alerting"], "candidates": cand[:25]})
            return {"sightings": out, "total_marks": total}

    def map(self):
        with self.db() as c:
            marks = [{"id": m["id"], "drive": m["drive"], "mark": m["mark"], "kind": m["kind"], "time": m["time"],
                      "lat": m["lat"], "lon": m["lon"], "lead": m["lead"]}
                     for m in self._marks(c) if m["lat"] is not None]
            alerts = [dict(r) for r in c.execute(
                "SELECT drive, time, tier, mac, label, ssid, best, lat, lon, from_lat, from_lon, to_lat, to_lon "
                "FROM alerts WHERE lat IS NOT NULL")]
        return {"marks": marks, "alerts": alerts, "fixed": [f for f in self.fixed() if f["stationary"]]}

    def drives(self):
        with self.db() as c:
            return [dict(r) for r in c.execute(
                "SELECT drive, MIN(time) AS first, COUNT(*) AS n, SUM(kind='m') AS marks, SUM(kind='a') AS alerts FROM ("
                " SELECT drive, time, 'm' AS kind FROM marks WHERE saw IS NOT 'AUTO' UNION ALL SELECT drive, time, 'a' FROM alerts"
                ") GROUP BY drive ORDER BY drive DESC")]

    def finder(self):
        """Signature finder: every detail logged about the devices in each
        snapshot, counted where you saw police versus everywhere else (other
        marks and background samples). What turns up at police sightings in
        several places and rarely otherwise is a candidate signature."""
        with self.db() as c:
            snaps = {m["id"]: m for m in self._marks(c, samples=True)}
            police = {i for i, m in snaps.items() if m["saw_effective"] == "POLICE"}
            base = set(snaps) - police
            feats = {}   # (type, value) -> {"police": set(mark ids), "base": set(mark ids), "ex": [...], "caught": bool}
            for l in c.execute("SELECT mark_id, kind, mac, rssi, ssid, name, cid, adv, wps, vie, frames, probes, "
                               "alerted FROM lines WHERE kind IN ('WIFI','BLE')"):
                mid = l["mark_id"]
                if mid not in snaps:
                    continue
                keys = []
                ssid, frames = l["ssid"], l["frames"] or ""
                if l["kind"] == "WIFI":
                    if ssid and ("B" in frames or "R" in frames):
                        keys.append(("Network name", ssid))
                    searched = set(l["probes"].split("|")) if l["probes"] else set()
                    if ssid and frames.replace("D", "").replace("M", "") == "P":
                        searched.add(ssid)
                    keys += [("Searched-for name", x) for x in searched]
                    for nm in ([ssid] if ssid else []) + list(searched):
                        pat = name_pattern(nm)
                        if pat:
                            keys.append(("Name pattern", pat))
                    if l["wps"] and l["wps"].strip(" /"):
                        keys.append(("WPS device", l["wps"].strip()))
                    for o in (l["vie"] or "").split(","):
                        if len(o) == 6:
                            keys.append(("Beacon vendor code", o.upper()))
                else:
                    if l["name"] and len(re.sub(r"[^0-9A-Za-z]", "", l["name"])) >= 3:   # not "." junk
                        keys.append(("Bluetooth name", l["name"]))
                    if l["cid"] and l["cid"].upper() not in ("FFFF", "0000"):   # placeholder IDs
                        keys.append(("Bluetooth company", l["cid"].upper()))
                    keys += [("Bluetooth service", f"{u:04X}") for u in ble_services(l["adv"])]
                v = self.vendors.for_mac(l["mac"])
                if v and v != "(random)":
                    keys.append(("Vendor", v))
                for k in keys:
                    e = feats.setdefault(k, {"police": set(), "base": set(), "ex": [], "caught": False, "macs": set()})
                    (e["police"] if mid in police else e["base"]).add(mid)
                    e["caught"] |= bool(l["alerted"])
                    e["macs"].add(l["mac"])
                    if mid in police and len(e["ex"]) < 3:
                        m = snaps[mid]
                        e["ex"].append({"mac": l["mac"], "rssi": l["rssi"], "drive": m["drive"], "mark": m["mark"]})
        n_pol, n_base = len(police), len(base)
        out = []
        for (typ, val), e in feats.items():
            if not e["police"]:
                continue
            # separate places (500 m apart) among the police sightings it was heard at
            # (marks without a GPS fix count together as at most one extra place)
            places, no_fix = [], False
            for mid in e["police"]:
                m = snaps[mid]
                if m["lat"] is None:
                    no_fix = True
                elif all(dist_m(m["lat"], m["lon"], p[0], p[1]) > 500 for p in places):
                    places.append((m["lat"], m["lon"]))
            if no_fix:
                places.append(None)
            drives = {snaps[i]["drive"] for i in e["police"]}
            rate_p = len(e["police"]) / max(n_pol, 1)
            rate_b = (len(e["base"]) + 0.5) / (n_base + 1)
            lift = rate_p / rate_b
            score = len(places) * math.log2(1 + lift) * (1.0 if len(drives) > 1 else 0.6)
            out.append({"type": typ, "value": val, "label": self._feature_label(typ, val),
                        "places": len(places), "drives": sorted(drives), "police_marks": len(e["police"]),
                        "base_hits": len(e["base"]), "lift": round(lift, 1), "score": round(score, 2),
                        "caught": e["caught"], "devices": len(e["macs"]), "examples": e["ex"],
                        "suggest": self._suggest(typ, val, e["macs"])})
        out.sort(key=lambda x: -x["score"])
        return {"candidates": out[:1000], "police_snapshots": n_pol, "baseline_snapshots": n_base}

    def _feature_label(self, typ, val):
        if typ == "Bluetooth company":
            return self.vendors.for_cid(val)
        if typ == "Beacon vendor code":
            return self.vendors.for_mac(val + "000000")
        return ""

    def _suggest(self, typ, val, macs):
        """A line to add to the firmware's tables (start every new one at MAYBE)."""
        q = lambda x: x.upper().replace('"', "'")
        if typ in ("Network name", "Searched-for name"):
            return f'{{"{q(val)}", "<label>", CAT_LOW_CONF, MATCH_EXACT}},   // ssidTargets'
        if typ == "Name pattern":
            prefix = val.replace("###", "")
            return f'{{"{q(prefix)}", "<label>", CAT_LOW_CONF, MATCH_PREFIX}},   // ssidTargets'
        if typ == "WPS device":
            part = val.split("/")[0].strip() or val
            return f'{{"{q(part)}", "<label>", CAT_LOW_CONF, MATCH_SUBSTRING}},   // wpsTargets'
        if typ == "Bluetooth company":
            return f'{{0x{val}, "<label>", CAT_LOW_CONF}},   // bleCompanyTargets'
        if typ == "Bluetooth service":
            return f'{{0x{val}, "<label>", CAT_LOW_CONF}},   // bleServiceTargets'
        if typ == "Bluetooth name":
            return f'{{"{q(val)}", "<label>", CAT_LOW_CONF}},   // bleNameTargets'
        if typ == "Vendor":
            pre = sorted({m.replace(":", "")[:6] for m in macs if not is_random(m)})
            return " ".join(f'{{0x{p}ULL, 24, "<label>", CAT_LOW_CONF}},' for p in pre[:3]) + "   // prefixTargets"
        return "(no table for this yet: ask Claude)"

    def export(self, drives):
        """One text file for Claude: the raw lines of the chosen drives, with vendor names added."""
        drives = sorted(set(drives))
        q = ",".join("?" * len(drives))
        with self.db() as c:
            saw = self._saw(c)
            out = ["# AirWatch export for analysis",
                   f"# drives: {', '.join(map(str, drives))}",
                   "# Each #MARK is a button press (saw=POLICE tap / NOTHING hold; type = whether the detector",
                   "# was alerting). Lines under it are everything heard in the 30 s before it (WIFI/BLE) and what",
                   "# was alerting (ALERT / EARLIER). '# vendor:' comments are added by the server from the IEEE",
                   "# registry and the Bluetooth company list. 'A' lines are the alert journal.", ""]
            for m in c.execute(f"SELECT * FROM marks WHERE drive IN ({q}) AND saw IS NOT 'AUTO' "
                               "ORDER BY time, drive, mark", drives):
                eff = saw.get((m["drive"], m["mark"]))
                out.append(m["header"] + (f"   # corrected: saw={eff}" if eff and eff != m["saw"] else ""))
                for l in c.execute("SELECT raw, mac, cid FROM lines WHERE mark_id=? ORDER BY id", (m["id"],)):
                    v = self.vendor(l["mac"], l["cid"])
                    out.append(l["raw"] + (f"   # vendor: {v}" if v and v != "(random)" else ""))
                for n in c.execute("SELECT raw FROM notes WHERE drive=? AND mark=?", (m["drive"], m["mark"])):
                    out.append(n["raw"])
                out.append("")
            alerts = list(c.execute(f"SELECT raw, mac FROM alerts WHERE drive IN ({q}) ORDER BY time, up", drives))
            if alerts:
                out.append("# ---- alert journal ----")
                for a in alerts:
                    v = self.vendor(a["mac"])
                    out.append(a["raw"] + (f"   # vendor: {v}" if v and v != "(random)" else ""))
        return "\n".join(out) + "\n"
