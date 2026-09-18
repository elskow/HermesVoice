#!/usr/bin/env python3
"""Derive PROJECT_VER from git, else fallback. Used by top CMakeLists."""
import re
import subprocess


def main() -> None:
    try:
        tag = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            stderr=subprocess.DEVNULL, text=True).strip()
    except Exception:
        tag = ""
    if re.fullmatch(r"\d+\.\d+\.\d+", tag or ""):
        print(tag)
    else:
        print("0.1.0-dev")


if __name__ == "__main__":
    main()
