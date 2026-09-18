#!/usr/bin/env python3
"""make doctor: check every onboarding prerequisite, print the install
command for whatever is missing. Native toolchain (no docker for the
build itself): ESP-IDF + USB flashing through containers is a worse
onboarding than the disease.

Each check is a function returning (ok, detail). To add one, append to
CHECKS: no shell quoting, no eval.
"""
import os
import platform
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOME = os.path.expanduser("~")


def go_version():
    """go >= 1.25 (relay/go.mod). Semantic compare, not regex."""
    try:
        with open(os.path.join(HERE, "relay", "go.mod")) as f:
            want = f.read().split("go ")[1].split()[0]
    except (OSError, IndexError):
        return False, "relay/go.mod unreadable"
    try:
        out = subprocess.run(["go", "version"], capture_output=True,
                             text=True, timeout=10).stdout
        have = re.search(r"go(\d+)\.(\d+)", out)
        if not have:
            return False, "go not on PATH: https://go.dev/dl"
        ok = (int(have[1]), int(have[2])) >= tuple(int(x) for x in want.split(".")[:2])
        return ok, f"have {have[1]}.{have[2]}, want >={want}"
    except (OSError, subprocess.SubprocessError):
        return False, "https://go.dev/dl"


def idf():
    """ESP-IDF 6.1 where the Makefile expects it (IDF_PATH)."""
    ok = (os.path.isfile(f"{HOME}/.espressif/v6.1/esp-idf/tools/idf.py")
          or os.path.isfile(f"{HOME}/.espressif/tools/python/v6.1/venv/bin/python"))
    return ok, ("~/.espressif/v6.1 present" if ok else
                "idf-install.sh, or https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/get-started/")


def xtensa():
    """xtensa esp32 gcc where the Makefile expects it (XTENSA)."""
    p = (f"{HOME}/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204"
         "/xtensa-esp-elf/bin/xtensa-esp32-elf-gcc")
    return os.access(p, os.X_OK), ("" if os.access(p, os.X_OK) else "bundled with the IDF install")


def qemu():
    """qemu xtensa (optional: only for make test-qemu)."""
    p = (f"{HOME}/.espressif/tools/qemu-xtensa/esp_develop_9.2.2_20260417"
         "/qemu/bin/qemu-system-xtensa")
    ok = shutil.which("qemu-system-xtensa") or os.access(p, os.X_OK)
    return bool(ok), ("" if ok else "idf_tools.py install qemu-xtensa (optional)")


def gcc_mac():
    """gcc-16 for the linux sim (macOS: Apple clang rejects IDF mbedtls flags)."""
    if platform.system() != "Darwin":
        return True, "linux: system gcc"
    ok = shutil.which("gcc-16") or os.access("/opt/homebrew/bin/gcc-16", os.X_OK)
    return bool(ok), ("" if ok else "brew install gcc (macOS only)")


def docker():
    """docker for the compose stack (optional)."""
    ok = shutil.which("docker")
    return bool(ok), ("" if ok else "https://docs.docker.com/get-docker")


def dotenv():
    """relay/.env exists with a real HERMES_API_KEY (not the empty example)."""
    path = os.path.join(HERE, "relay", ".env")
    try:
        with open(path) as f:
            for line in f:
                if line.startswith("HERMES_API_KEY=") and line.strip()[15:]:
                    return True, "key present"
    except OSError:
        pass
    return False, "cp relay/.env.example relay/.env, fill keys (docs/setup.md#keys)"


def templ():
    """templ for UI work (optional: only to edit *.templ)."""
    ok = shutil.which("templ") or os.access(f"{HOME}/go/bin/templ", os.X_OK)
    return bool(ok), ("" if ok else
                      "go install github.com/a-h/templ/cmd/templ@latest")


def air():
    """air for live reload (optional: only for make watch)."""
    ok = shutil.which("air")
    return bool(ok), ("" if ok else
                      "go install github.com/air-verse/air@latest")


CHECKS = [go_version, idf, xtensa, qemu, gcc_mac, docker, dotenv, templ, air]


def main():
    fail = 0
    for check in CHECKS:
        ok, detail = check()
        label = check.__doc__.split("\n")[0].rstrip(".")
        suffix = f": {detail}" if (detail and not ok) else ""
        print(f"{'ok  ' if ok else 'MISS'}  {label}{suffix}")
        fail |= not ok
    print("doctor: all green" if not fail else "doctor: fix MISS lines above")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
