#!/usr/bin/env python3
"""Independent REAL WebRTC processes, one result per scenario AND both roles.
No runtime substitution. Probe exit 77 is SKIP, never PASS. Child assertions
require fresh decoded VP8 pixels, not just status or capability flags.
"""
from __future__ import annotations
import json
import pathlib
import subprocess
import sys
import time

SCENARIOS = ("one-way", "duplex", "two-duplex", "stop-one", "regrant",
             "close-new", "slot-transfer", "limit-three", "metadata-late",
             "isolation", "lifecycle", "reconnect")
PHASES = {"one-way": 1, "duplex": 1, "two-duplex": 1, "stop-one": 2,
          "regrant": 3, "close-new": 2, "slot-transfer": 4, "limit-three": 1,
          "metadata-late": 3, "isolation": 4, "lifecycle": 6, "reconnect": 1,
          "early-ice": 1, "delayed-answer": 1, "duplicate-eoc": 1}


def terminate(child: subprocess.Popen | None) -> None:
    if child is not None and child.poll() is None:
        child.terminate()
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait(timeout=5)


def validate(text: str, code: int | None, scenario: str) -> str | None:
    rounds = 2 if scenario == "reconnect" else 1
    if code != 0:
        return f"child exit={code}"
    for round_number in range(rounds):
        if text.count(f"WEBRTC_OK scenario={scenario} round={round_number} ") != 1:
            return f"missing/duplicate actual decode completion for round {round_number}"
        for phase in range(PHASES[scenario]):
            matches = [s for s in text.splitlines() if s.startswith(f"PASS phase={phase} ")
                       and f" round={round_number} " in s]
            if len(matches) != 1:
                return f"missing/duplicate PASS for phase {phase} round {round_number}"
    if scenario == "reconnect" and "RECONNECT_OK distinct session IDs" not in text:
        return "missing same-process reconnect assertion"
    if "FAIL " in text:
        return "child reports a failed assertion"
    return None


def run(a: str, b: str, out: pathlib.Path, scenarios: tuple[str, ...] = SCENARIOS) -> int:
    if not scenarios or any(s not in PHASES for s in scenarios):
        raise ValueError("unknown/empty scenario selection")
    out.mkdir(parents=True, exist_ok=True)
    results: list[dict] = []
    summary = {"kind": "real-webrtc", "status": "RUNNING", "results": results}
    def save() -> None:
        (out / "results.json").write_text(json.dumps(summary, indent=2) + "\n")
    save()
    for number, binary in enumerate(dict.fromkeys((a, b))):
        result = subprocess.run([binary, "probe"], text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=20)
        (out / f"probe-{number}.log").write_text(result.stdout)
        if result.returncode:
            summary.update(status="SKIP" if result.returncode == 77 else "FAIL",
                           reason="genuine backend probe", exit=result.returncode)
            save()
            print(result.stdout, end="")
            return result.returncode
    roles = ((a, b, "egor", "stepan"), (b, a, "stepan", "egor"))
    for scenario in scenarios:
        for index, (hb, gb, hn, gn) in enumerate(roles):
            folder = out / f"{scenario}-{index}-{hn}-host"
            folder.mkdir(exist_ok=True)
            host_log, guest_log = folder / "host.log", folder / "guest.log"
            host = guest = None
            item = {"scenario": scenario, "host": hn, "guest": gn, "status": "FAIL",
                    "logs": str(folder.relative_to(out))}
            started = time.monotonic()
            try:
                with host_log.open("w") as hf, guest_log.open("w") as gf:
                    host = subprocess.Popen([hb, "host", "127.0.0.1", "0", hn, scenario],
                                            stdout=hf, stderr=subprocess.STDOUT)
                    port = None
                    until = time.monotonic() + 12
                    while port is None and time.monotonic() < until and host.poll() is None:
                        for line in host_log.read_text(errors="replace").splitlines():
                            if line.startswith("LISTEN "):
                                port = line.split()[1]
                                break
                        time.sleep(.02)
                    if port is None:
                        raise RuntimeError("host did not report its bound TCP port")
                    guest = subprocess.Popen([gb, "join", "127.0.0.1", port, gn, scenario],
                                             stdout=gf, stderr=subprocess.STDOUT)
                    # Each child retains its 15-second phase and 45-second round
                    # assertions. This is an outer process cleanup bound only.
                    until = time.monotonic() + (100 if scenario == "reconnect" else 55)
                    while time.monotonic() < until:
                        if host.poll() is not None and guest.poll() is not None:
                            break
                        # A failed child must not keep its peer running pointlessly.
                        if host.poll() not in (None, 0) or guest.poll() not in (None, 0):
                            break
                        time.sleep(.02)
                    if host.poll() is None or guest.poll() is None:
                        raise RuntimeError("peer failed or exceeded outer process deadline")
                item.update(host_exit=host.returncode, guest_exit=guest.returncode)
                failures = []
                for name, path, code in ((hn, host_log, host.returncode), (gn, guest_log, guest.returncode)):
                    why = validate(path.read_text(errors="replace"), code, scenario)
                    if why:
                        failures.append(name + ": " + why)
                if failures:
                    raise RuntimeError("; ".join(failures))
                item["status"] = "PASS"
            except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
                item["reason"] = str(exc)
            finally:
                terminate(guest)
                terminate(host)
                item.update(host_exit=host.returncode if host else None,
                            guest_exit=guest.returncode if guest else None,
                            seconds=round(time.monotonic() - started, 3))
            (folder / "result.json").write_text(json.dumps(item, indent=2) + "\n")
            results.append(item)
            print(f"{item['status']} {scenario}: {hn} Host / {gn} Join; {folder}", flush=True)
            save()  # Never let an early PASS hide another case/role's failure.
    success = all(r["status"] == "PASS" for r in results)
    summary.update(status="PASS" if success else "FAIL", exit=0 if success else 1)
    save()
    return 0 if success else 1


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        raise SystemExit("usage: media_process_test.py binary-a binary-b logs [scenario]")
    try:
        raise SystemExit(run(str(pathlib.Path(sys.argv[1]).resolve()),
                             str(pathlib.Path(sys.argv[2]).resolve()), pathlib.Path(sys.argv[3]),
                             (sys.argv[4],) if len(sys.argv) == 5 else SCENARIOS))
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"FAIL driver: {exc}", file=sys.stderr)
        raise SystemExit(1)
