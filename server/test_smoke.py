"""Smoke test for CI: parse sample logs, index them, run every analysis."""
import os
import tempfile

import logparse
from store import Store
from vendors import Vendors

MARKS = """#MARK drive=7 mark=1 saw=POLICE type=DURING_ALERT uptime=480s heard=3 obs_dropped=2 drops=1 ble_drops=0 time=2026-11-25T18:29:18Z lat=37.64214 lon=-77.49438 mph=21 alerting=LOW lead="Cradlepoint GW" in_range=1
WIFI 00:30:44:B1:F8:1B frames=BR ch=6 rssi=-56 secs=9 ago=1844ms first=16780ms ssid="GRTC-MT Wifi" vie=8CFDF0 alerted=LOW
WIFI 10:74:6F:BB:F1:1A frames=P ch=1 rssi=-82 secs=1 ago=19ms first=19ms ssid="GRTC-Radio" probes="GRTC-Radio|govt"
BLE  00:25:DF:A3:CC:B6 addr=public rssi=-98 secs=1 ago=6658ms first=6658ms adv=0201060303FDFE
ALERT 00:30:44:B1:F8:1B tier=LOW src=WIFI rssi=-59 ch=6 ago=1844ms seen=21 in_range_for=14s beeped=yes label="Cradlepoint GW" ssid="GRTC-MT Wifi" best=-55 at=37.64202,-77.49477 mph=32 tl=-80,,-60,-55 from=37.642,-77.495 to=37.6423,-77.4941 why=[PRF_LO|]
#NOTE drive=7 mark=1 saw=POLICE text="Henrico PD truck"
"""
SAMPLES = """#SAMPLE drive=8 n=1 type=NO_ALERT uptime=400s heard=1 obs_dropped=0 time=2026-11-26T12:00:00Z lat=37.7 lon=-77.4 mph=45 alerting=NONE
WIFI AC:91:9B:C6:25:94 frames=BR ch=1 rssi=-70 secs=5 ago=500ms first=20000ms ssid="Verizon_meena"
"""
ALERTS = """A drive=7 up=467 time=2026-11-25T18:29:02Z tier=LOW src=WIFI mac=00:30:44:B1:F8:1B best=-48 ch=6 dur=32 seen=44 label="Cradlepoint GW" ssid="GRTC-MT Wifi" at=37.64222,-77.49419 mph=8 tl=-70,-60 from=37.642,-77.495 to=37.6423,-77.4941 why=[PRF_LO|]
"""

marks, notes = logparse.parse_marks(MARKS)
assert len(marks) == 1 and len(marks[0]["lines"]) == 4 and notes[0]["text"] == "Henrico PD truck"
assert marks[0]["lines"][1]["probes"] == ["GRTC-Radio", "govt"]
assert marks[0]["lines"][3]["tl"] == [-80, None, -60, -55]
assert logparse.parse_marks(SAMPLES)[0][0]["saw"] == "AUTO"
assert logparse.parse_alerts(ALERTS)[0]["tl"] == [-70, -60]

with tempfile.TemporaryDirectory() as d:
    day = os.path.join(d, "2026-11-26")
    os.makedirs(day)
    for name, text in (("1_drive7_marks.log", MARKS), ("2_drive8_samples.log", SAMPLES), ("3_drive7_alerts.log", ALERTS)):
        with open(os.path.join(day, name), "w") as f:
            f.write(text)
    s = Store(d, Vendors(d, log=lambda *a: None), log=lambda *a: None)
    s.ingest_all()
    assert s.summary()["marks"] == 1 and s.summary()["samples"] == 1
    for fn in (s.marks, s.journal, s.repeat, s.fixed, s.police, s.map, s.drives, s.finder):
        fn()
    assert s.mark(s.marks()[0]["id"])["lines"]
    assert "GRTC-MT Wifi" in s.export([7])
    assert s.drops() == [{"drive": 7, "drops": 1, "ble_drops": 0, "obs_dropped": 2}]
    assert any(c["value"] == "govt" for c in s.finder()["candidates"])
print("smoke test ok")
