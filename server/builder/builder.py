#!/usr/bin/env python3
"""AirWatch firmware builder.

Every AIRWATCH_INTERVAL_MIN minutes (and at start-up) it fetches the public
repository and builds the firmware when either the firmware folder
(airwatch_v2/) or your secrets.h has changed since the last build. The result
replaces <data>/firmware/current.bin + current.json: only the newest build is
kept. The receiver serves it at /firmware and the board installs it the next
time it arrives home.

Build name: <short commit of the last firmware change>.<4 hex of secrets.h>,
so editing secrets.h (new Wi-Fi password, say) also reaches the board, and a
commit that only touches the server does not cause an update.

Settings (environment):
  AIRWATCH_REPO          default https://github.com/nullexxx/airwatch.git
  AIRWATCH_BRANCH        default main
  AIRWATCH_INTERVAL_MIN  default 30
  AIRWATCH_SECRETS       default /secrets/secrets.h
  AIRWATCH_DATA          default /data
"""
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import time

REPO = os.environ.get("AIRWATCH_REPO", "https://github.com/nullexxx/airwatch.git")
BRANCH = os.environ.get("AIRWATCH_BRANCH", "main")
INTERVAL = max(5, int(os.environ.get("AIRWATCH_INTERVAL_MIN", "30"))) * 60
SECRETS = os.environ.get("AIRWATCH_SECRETS", "/secrets/secrets.h")
DATA = os.environ.get("AIRWATCH_DATA", "/data")
OUT = os.path.join(DATA, "firmware")
WORK = os.environ.get("AIRWATCH_WORK", "/work")
SRC = os.path.join(WORK, "src")
BUILD = os.path.join(WORK, "build")
FQBN = "esp32:esp32:XIAO_ESP32S3:PSRAM=opi"
APP_SLOT = 0x200000          # partitions.csv: each firmware slot is 2 MB


def now():
    return datetime.datetime.now().isoformat(timespec="seconds")


def log(msg):
    print(f"{now()} {msg}", flush=True)


def run(*cmd, cwd=None, timeout=1800):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)
    if p.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd[:3])} failed:\n{(p.stdout + p.stderr)[-1500:]}")
    return p.stdout.strip()


def write_json(name, obj):
    os.makedirs(OUT, exist_ok=True)
    tmp = os.path.join(OUT, name + ".tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=1)
    os.replace(tmp, os.path.join(OUT, name))


def status(result, **extra):
    write_json("status.json", {"result": result, "at": now(), **extra})


def current_build():
    try:
        with open(os.path.join(OUT, "current.json"), encoding="utf-8") as f:
            return json.load(f).get("build")
    except (OSError, ValueError):
        return None


def update_source():
    if not os.path.isdir(os.path.join(SRC, ".git")):
        shutil.rmtree(SRC, ignore_errors=True)
        run("git", "clone", "--filter=blob:none", "--branch", BRANCH, REPO, SRC, timeout=600)
    else:
        run("git", "fetch", "--filter=blob:none", "origin", BRANCH, cwd=SRC, timeout=600)
        run("git", "reset", "--hard", "FETCH_HEAD", cwd=SRC)


def check_once():
    if not os.path.isfile(SECRETS):
        status("waiting", error=f"no secrets.h at {SECRETS}: mount yours there (read-only)")
        log(f"no {SECRETS} yet - nothing to build")
        return
    with open(SECRETS, "rb") as f:
        secrets = f.read()
    if b"UPLOAD_URL" not in secrets:
        log("warning: secrets.h has no UPLOAD_URL - a board running this build cannot update itself")

    update_source()
    commit = run("git", "log", "-1", "--format=%h", "--", "airwatch_v2", cwd=SRC)
    subject = run("git", "log", "-1", "--format=%s", "--", "airwatch_v2", cwd=SRC)
    build = f"{commit}.{hashlib.sha256(secrets).hexdigest()[:4]}"
    if build == current_build():
        status("up to date", build=build)
        log(f"{build} already built")
        return

    log(f"building {build}: {subject}")
    status("building", build=build)
    sketch = os.path.join(BUILD, "airwatch_v2")
    shutil.rmtree(BUILD, ignore_errors=True)
    shutil.copytree(os.path.join(SRC, "airwatch_v2"), sketch)
    with open(os.path.join(sketch, "secrets.h"), "wb") as f:
        f.write(secrets)
    with open(os.path.join(sketch, "build_id.h"), "w", encoding="utf-8") as f:
        f.write(f'#define AIRWATCH_BUILD "{build}"\n')
    try:
        run("arduino-cli", "compile", "--fqbn", FQBN, "--output-dir", os.path.join(BUILD, "out"), sketch)
        with open(os.path.join(BUILD, "out", "airwatch_v2.ino.bin"), "rb") as f:
            image = f.read()
        if not image or len(image) > APP_SLOT:
            raise RuntimeError(f"firmware is {len(image)} bytes; a slot holds {APP_SLOT}")
    except Exception as e:
        status("failed", build=build, error=str(e)[-1500:])
        log(f"build {build} FAILED - the board keeps the previous one\n{e}")
        return
    finally:
        # the copy with your passwords is not kept around
        shutil.rmtree(sketch, ignore_errors=True)

    os.makedirs(OUT, exist_ok=True)
    tmp = os.path.join(OUT, "current.bin.tmp")
    with open(tmp, "wb") as f:
        f.write(image)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, os.path.join(OUT, "current.bin"))   # newest only: replaces the last one
    write_json("current.json", {"build": build, "md5": hashlib.md5(image).hexdigest(), "size": len(image),
                                "built_at": now(), "commit": commit, "message": subject})
    shutil.rmtree(os.path.join(BUILD, "out"), ignore_errors=True)
    status("built", build=build)
    log(f"built {build} ({len(image):,} bytes): the board installs it when it next gets home")


def main():
    if os.environ.get("AIRWATCH_ONCE"):     # one check, then exit (testing)
        return check_once()
    log(f"watching {REPO} ({BRANCH}) every {INTERVAL // 60} min")
    while True:
        try:
            check_once()
        except Exception as e:     # network down, GitHub unreachable ...: try again later
            status("error", error=str(e)[-1500:])
            log(f"check failed: {e}")
        time.sleep(INTERVAL)


if __name__ == "__main__":
    main()
