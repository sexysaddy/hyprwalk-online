#!/usr/bin/env python3
"""Exercise independent UDP/world test executables over loopback; not a Hyprland test."""
import json
import pathlib
import subprocess
import sys
import time


def records(path: pathlib.Path, prefix: str):
    found = []
    for line in path.read_text(errors="replace").splitlines():
        if line.startswith(prefix):
            found.append(json.loads(line[len(prefix):]))
    return found


def run_pair(host_binary: pathlib.Path, client_binary: pathlib.Path,
             host_name: str, client_name: str, logs: pathlib.Path):
    host_log = logs / f"{host_name}-host.log"
    client_log = logs / f"{client_name}-client.log"
    processes = []
    try:
        with host_log.open("w") as host_output, client_log.open("w") as client_output:
            host = subprocess.Popen([str(host_binary), "host", "127.0.0.1", "0", host_name, "--once"],
                                    stdout=host_output, stderr=subprocess.STDOUT)
            processes.append(host)
            deadline = time.monotonic() + 10
            port = None
            while time.monotonic() < deadline:
                for status in records(host_log, "STATUS "):
                    if status["state"] == "listening":
                        port = status["port"]
                if port:
                    break
                if host.poll() is not None:
                    raise RuntimeError(f"host exited before listening: {host_log.read_text()}")
                time.sleep(0.005)
            if not port:
                raise TimeoutError("host did not report a listening port")
            client = subprocess.Popen([str(client_binary), "join", "127.0.0.1", str(port), client_name, "--once"],
                                      stdout=client_output, stderr=subprocess.STDOUT)
            processes.append(client)
            exits = [client.wait(timeout=18), host.wait(timeout=18)]
        if exits != [0, 0]:
            raise RuntimeError(f"process exits {exits}; inspect {host_log} and {client_log}")
        h = next(s for s in records(host_log, "STATUS ") if s["state"] == "connected")
        c = next(s for s in records(client_log, "STATUS ") if s["state"] == "connected")
        assert h["session"] == c["session"]
        assert h["peer_id"] == c["local_id"] and c["peer_id"] == h["local_id"]
        assert h["local_id"] != c["local_id"]
        assert h["peer_name"] == client_name and c["peer_name"] == host_name
        for log in (host_log, client_log):
            exchanges = records(log, "WORLD_UDP_OK ")
            statuses = records(log, "STATUS ")
            assert exchanges and exchanges[-1]["test_received"] >= 2 and exchanges[-1]["test_acked"] >= 2
            # The peer that finishes first may leave immediately after WORLD_UDP_OK.
            # In that case the peer's WORLD_UDP_OK status can already reflect the
            # teardown state. Validate READY and traffic counters against the full
            # status history instead of incorrectly requiring the final event to be
            # captured before either process starts teardown.
            ready = [s for s in statuses if s.get("world_state") == "ready"
                     and s.get("udp_state") == "ready" and s.get("capabilities") == 7]
            assert ready, f"no ready world/UDP status in {log}"
            samples = [(int(parts[1].split("=", 1)[1]), int(parts[2].split("=", 1)[1]))
                       for line in log.read_text().splitlines() if line.startswith("SAMPLES received=")
                       for parts in [line.split()] if len(parts) == 3]
            assert samples and any(received >= 35 and rendered >= 20 for received, rendered in samples)
        return {"host": host_name, "client": client_name, "session": h["session"],
                "host_binary": str(host_binary), "client_binary": str(client_binary), "passed": True}
    finally:
        # Only our own test children are terminated, and always reaped.
        for process in processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


def main():
    first = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/player_cli").resolve()
    second = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else str(first)).resolve()
    logs = pathlib.Path(sys.argv[3] if len(sys.argv) > 3 else "build/player-process-logs").resolve()
    logs.mkdir(parents=True, exist_ok=True)
    for binary in (first, second):
        if not binary.is_file():
            raise FileNotFoundError(binary)
    results = [run_pair(first, second, "egor", "stepan", logs),
               run_pair(second, first, "stepan", "egor", logs)]
    (logs / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
