#!/usr/bin/env python3
"""
Usage:
  ./tools/dev.py [--expect TEXT]          full stack (relay + sim)
  ./tools/dev.py --sim-only [--relay URL] sim against a running relay
"""
import argparse
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def dotenv_val(key):
    if os.environ.get(key):
        return os.environ[key]
    try:
        with open(os.path.join(HERE, "relay", ".env")) as f:
            for line in f:
                if line.startswith(key + "="):
                    return line[len(key) + 1:].strip()
    except OSError:
        pass
    return ""


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def preflight(base_url, api_key):
    req = urllib.request.Request(
        base_url.rstrip("/") + "/v1/models",
        headers={"Authorization": "Bearer " + api_key} if api_key else {})
    try:
        with urllib.request.urlopen(req, timeout=5):
            return True
    except Exception:
        return False


def wait_relay(port, proc):
    probe = urllib.request.Request(
        f"http://127.0.0.1:{port}/v1/voice", data=b"",
        headers={"X-Device-ID": "probe",
                 "Content-Type": "audio/l16;rate=16000;channels=1"})
    for _ in range(40):
        if proc.poll() is not None:
            print("FAIL: relay exited early. tail /tmp/dev-relay.log:")
            tail("/tmp/dev-relay.log", 5)
            return False
        try:
            with urllib.request.urlopen(probe, timeout=3):
                return True
        except Exception:
            time.sleep(0.5)
    print("FAIL: relay never came up. tail /tmp/dev-relay.log:")
    tail("/tmp/dev-relay.log", 5)
    return False


def tail(path, n):
    try:
        with open(path, "rb") as f:
            lines = f.read().splitlines()[-n:]
        for line in lines:
            sys.stdout.write(line.decode("utf-8", "replace") + "\n")
    except OSError:
        pass


def run_sim(relay_url, expect):
    elf = os.path.join(HERE, "firmware", "build-linux", "voice-node.elf")
    if not os.access(elf, os.X_OK):
        print(f"FAIL: {elf} missing. Build it first: make sim-build")
        return 1
    env = dict(os.environ,
               NODE_GATEWAY_URL=relay_url,
               SIM_SCRIPT=os.path.join(HERE, "firmware", "tools", "sim_press.txt"),
               SIM_LINGER_MS="15000")
    with open("/tmp/dev-sim.log", "wb") as log:
        try:
            subprocess.run([elf], env=env, stdout=log, stderr=log,
                           timeout=120)
        except subprocess.TimeoutExpired:
            pass
    shown = 0
    want = re.compile(rb"replaying|recording|uploading|status=|heard:|hermes:")
    with open("/tmp/dev-sim.log", "rb") as f:
        for line in f:
            if want.search(line) and shown < 10:
                sys.stdout.write(line.decode("utf-8", "replace"))
                shown += 1
    with open("/tmp/dev-sim.log", "rb") as f:
        if expect.encode() in f.read():
            print(f"DEV STACK GREEN (expect: {expect})")
            return 0
    print(f"FAIL: expected {expect!r} not in sim output. Full output above.")
    print("--- relay tail:")
    tail("/tmp/dev-relay.log", 5)
    return 1


def main():
    ap = argparse.ArgumentParser(description="Local relay+sim dev stack")
    ap.add_argument("--expect", default="heard:")
    ap.add_argument("--sim-only", action="store_true",
                    help="skip relay spawn + preflight, use --relay")
    ap.add_argument("--relay", default="",
                    help="relay URL for --sim-only (default http://127.0.0.1:8081)")
    args = ap.parse_args()

    if args.sim_only:
        return run_sim(args.relay or "http://127.0.0.1:8081", args.expect)

    base = (os.environ.get("HERMES_BASE_URL")
            or dotenv_val("HERMES_BASE_URL")).replace(
                "host.docker.internal", "127.0.0.1")
    key = os.environ.get("HERMES_API_KEY") or dotenv_val("HERMES_API_KEY")
    if not base:
        print("FAIL: no HERMES_BASE_URL in env or relay/.env "
              "(copy relay/.env.example)")
        return 1
    # Export resolved values: env wins in Go (godotenv fills the rest).
    os.environ["HERMES_BASE_URL"] = base
    os.environ["HERMES_API_KEY"] = key

    port = free_port()
    stt = os.environ.get("STT_PROVIDER") or dotenv_val("STT_PROVIDER") or "stub"
    print(f"hermes={base} relay=:{port} stt={stt}", flush=True)

    if not preflight(base, key):
        print(f"FAIL: Hermes gateway unreachable at {base}/v1/models")
        print("  Is the gateway up? If it needs a key, set HERMES_API_KEY"
              " (env wins, else relay/.env).")
        return 1

    tmp = tempfile.mkdtemp(prefix="dev-relay-")
    binary = os.path.join(tmp, "hermes-voice")
    build = subprocess.run(
        ["go", "build", "-o", binary, "./relay/cmd/hermes-voice"],
        cwd=HERE, capture_output=True)
    if build.returncode != 0:
        build = subprocess.run(
            ["go", "build", "-o", binary, "./cmd/hermes-voice"],
            cwd=os.path.join(HERE, "relay"), capture_output=True)
        if build.returncode != 0:
            print("FAIL: relay build failed:")
            sys.stdout.write(build.stderr.decode()[-2000:])
            return 1
    env = dict(os.environ, PORT=str(port), MQTT_BROKER_URL="")
    with open("/tmp/dev-relay.log", "wb") as log:
        proc = subprocess.Popen([binary], env=env, stdout=log, stderr=log,
                                stdin=subprocess.DEVNULL, start_new_session=True)
    try:
        if not wait_relay(port, proc):
            return 1
        return run_sim(f"http://127.0.0.1:{port}", args.expect)
    finally:
        proc.terminate()


if __name__ == "__main__":
    sys.exit(main())
