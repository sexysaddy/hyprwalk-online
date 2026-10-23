#!/usr/bin/env python3
"""REAL ICE/DTLS/SRTP + VP8. Uses the same bounded per-case process supervisor.
The media_ice_cli delays real SDP Answers and duplicates real EOC at the TCP
transport boundary. It never fakes callbacks, descriptions or decoded frames.
"""
from __future__ import annotations
import pathlib
import subprocess
import sys
from media_process_test import run as run_scenarios

SCENARIOS = ("early-ice", "delayed-answer", "duplicate-eoc", "two-duplex",
             "regrant", "slot-transfer", "metadata-late", "reconnect")

def run(a: str, b: str, out: pathlib.Path) -> int:
    return run_scenarios(a, b, out, SCENARIOS)

if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        raise SystemExit("usage: media_ice_process_test.py binary-a binary-b logs [scenario]")
    try:
        raise SystemExit(run_scenarios(str(pathlib.Path(sys.argv[1]).resolve()),
                                       str(pathlib.Path(sys.argv[2]).resolve()), pathlib.Path(sys.argv[3]),
                                       (sys.argv[4],) if len(sys.argv) == 5 else SCENARIOS))
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"FAIL driver: {exc}", file=sys.stderr)
        raise SystemExit(1)
