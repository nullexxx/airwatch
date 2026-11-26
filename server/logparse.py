"""Parsers for the board's mark log and alert journal.

Mark log:
  #MARK drive=.. mark=.. saw=POLICE|NOTHING type=DURING_ALERT|NO_ALERT ...
  WIFI <mac> frames=.. ch=.. rssi=.. ... ssid=".." ...   (heard in the 30 s window)
  BLE  <mac> addr=public|random rssi=.. ... cid=.. svc=.. name=".." adv=..
  ALERT|EARLIER <mac> tier=.. src=.. rssi=.. label=".." at=lat,lon mph=.. why=[..]
  #NOTE drive=.. mark=.. saw=.. text=".."
Alert journal:
  A drive=.. up=.. time=.. tier=.. src=.. mac=.. best=.. ... at=lat,lon mph=.. why=[..]
"""
import re

_KV = re.compile(r'(\w+)=("([^"]*)"|\[[^\]]*\]|\S+)')


def kv(text):
    out = {}
    for m in _KV.finditer(text):
        out[m.group(1)] = m.group(3) if m.group(3) is not None else m.group(2)
    return out


def _int(v, default=None):
    try:
        return int(v)
    except (TypeError, ValueError):
        return default


def _float(v, default=None):
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def _ms(v):
    """'1234ms' -> 1234"""
    if v is None:
        return None
    return _int(str(v).rstrip("ms"))


def _latlon(at):
    if not at or "," not in at:
        return None, None
    a, b = at.split(",", 1)
    return _float(a), _float(b)


def _timeline(v):
    """'-80,,-70' -> [-80, None, -70] (strongest signal per second; None = not heard)"""
    if not v:
        return None
    return [_int(x) if x else None for x in v.split(",")]


def _speed(d):
    if "mph" in d:
        return _float(d["mph"])
    if "spd" in d:  # very old logs: km/h
        v = _float(d["spd"])
        return None if v is None else round(v / 1.609, 1)
    return None


def parse_marks(text):
    """Returns (marks, notes). Each mark: dict with header fields and 'lines'."""
    marks, notes, cur = [], [], None
    for raw in text.splitlines():
        line = raw.rstrip("\r")
        if not line:
            continue
        if line.startswith("#MARK") or line.startswith("#SAMPLE"):
            d = kv(line)
            sample = line.startswith("#SAMPLE")   # background sample: no button, saw=AUTO
            cur = {
                "drive": _int(d.get("drive")), "mark": _int(d.get("n" if sample else "mark")),
                "saw": "AUTO" if sample else d.get("saw"), "type": d.get("type"),
                "uptime": _int(str(d.get("uptime", "")).rstrip("s")),
                "heard": _int(d.get("heard")), "time": d.get("time"),
                "lat": _float(d.get("lat")), "lon": _float(d.get("lon")), "mph": _speed(d),
                "alerting": d.get("alerting"), "lead": d.get("lead") or d.get("last_lead"),
                "last_alert": d.get("last_alert"), "followup_of": _int(d.get("followup_of")),
                "header": line, "lines": [],
            }
            marks.append(cur)
        elif line.startswith("#NOTE"):
            d = kv(line)
            notes.append({"drive": _int(d.get("drive")), "mark": _int(d.get("mark")),
                          "saw": d.get("saw"), "text": d.get("text"), "raw": line})
        elif cur is not None:
            parts = line.split(None, 2)
            if len(parts) < 2 or parts[0] not in ("WIFI", "BLE", "ALERT", "EARLIER"):
                continue
            d = kv(parts[2] if len(parts) > 2 else "")
            lat, lon = _latlon(d.get("at"))
            cur["lines"].append({
                "kind": parts[0], "mac": parts[1].upper(),
                "rssi": _int(d.get("rssi")), "best": _int(d.get("best")),
                "ch": _int(d.get("ch")), "ssid": d.get("ssid"), "name": d.get("name"),
                "cid": d.get("cid"), "svc": d.get("svc"), "wps": d.get("wps"), "vie": d.get("vie"),
                "frames": d.get("frames"), "peer": (d.get("peer") or "").upper() or None,
                "secs": _int(d.get("secs")), "first_ms": _ms(d.get("first")), "ago_ms": _ms(d.get("ago")),
                "addr": d.get("addr"), "adv": d.get("adv"),
                "alerted": d.get("alerted"), "tier": d.get("tier"), "src": d.get("src"),
                "label": d.get("label"), "why": d.get("why"),
                "at_lat": lat, "at_lon": lon, "at_mph": _speed(d) if d.get("at") else None,
                "tl": _timeline(d.get("tl")), "from": _latlon(d.get("from")), "to": _latlon(d.get("to")),
                "probes": [x for x in (d.get("probes") or "").split("|") if x] or None,
                "raw": line,
            })
    return marks, notes


def parse_alerts(text):
    out = []
    for raw in text.splitlines():
        line = raw.rstrip("\r")
        if not line.startswith("A "):
            continue
        d = kv(line[2:])
        lat, lon = _latlon(d.get("at"))
        out.append({
            "drive": _int(d.get("drive")), "up": _int(d.get("up")), "time": d.get("time"),
            "tier": d.get("tier"), "src": d.get("src"), "mac": (d.get("mac") or "").upper(),
            "best": _int(d.get("best")), "ch": _int(d.get("ch")), "dur": _int(d.get("dur")),
            "seen": _int(d.get("seen")), "label": d.get("label"), "ssid": d.get("ssid"),
            "lat": lat, "lon": lon, "mph": _speed(d),
            "muted": d.get("muted") == "yes", "ongoing": d.get("ongoing"), "ended": d.get("ended"),
            "why": d.get("why"), "tl": _timeline(d.get("tl")),
            "from": _latlon(d.get("from")), "to": _latlon(d.get("to")), "raw": line,
        })
    return out
