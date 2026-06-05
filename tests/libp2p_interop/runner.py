#!/usr/bin/env python3
import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional


SCENARIOS = ("ping", "identify", "autonatv2", "relay_reserve", "unknown_protocol")
TCP_DIRECT_SCENARIOS = ("ping", "identify", "echo")
DHT_SCENARIOS = ("dht_find_peer", "dht_provide_find_provider")
RENDEZVOUS_SCENARIOS = ("rendezvous_register_discover",)
PUBSUB_SCENARIOS = ("gossipsub_publish",)
PUBSUB_STRESS_SCENARIO = "gossipsub_mixed_mesh_stress"
TOPOLOGY_SCENARIOS = ("relay_echo_topology", "dcutr_relay_topology")
DIAL_TIMEOUT_SECONDS = 90
NATIVE_TOPOLOGIES = (
    ("fcl", "go", "go"),
    ("go", "fcl", "fcl"),
    ("fcl", "rust", "rust"),
    ("rust", "fcl", "fcl"),
)


def run(command: list[str], cwd: Optional[Path] = None, env: Optional[dict[str, str]] = None) -> None:
    subprocess.run(command, cwd=cwd, env=env, check=True)


def enabled_from_args(value: str) -> bool:
    env = os.environ.get("FCL_ENABLE_LIBP2P_INTEROP")
    if env is not None:
        return env in ("1", "ON", "on", "true", "TRUE", "yes", "YES")
    return value in ("1", "ON", "on", "true", "TRUE", "yes", "YES")


def require_tool(name: str) -> str:
    path = shutil.which(name)
    if not path:
        raise RuntimeError(f"required tool is missing: {name}")
    return path


def require_donor(root: Path, name: str) -> Path:
    path = root / name
    if not path.exists():
        raise RuntimeError(f"required donor repository is missing: {path}")
    return path


def wait_json(path: Path, timeout: float) -> dict:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            return json.loads(path.read_text())
        time.sleep(0.05)
    raise TimeoutError(f"timed out waiting for {path}")


def tail_text(path: Path, limit: int = 20) -> str:
    if not path.exists():
        return "<missing log>"
    lines = path.read_text(errors="replace").splitlines()
    return "\n".join(lines[-limit:])


class Listener:
    def __init__(self, process: subprocess.Popen, ready: dict, stop_file: Path, log_file: Path, log_handle,
                 command: list[str]):
        self.process = process
        self.ready = ready
        self.stop_file = stop_file
        self.log_file = log_file
        self.log_handle = log_handle
        self.command = command

    def close(self) -> None:
        try:
            self.stop_file.write_text("stop\n")
            self.process.wait(timeout=5)
        except Exception:
            self.process.send_signal(signal.SIGTERM)
            try:
                self.process.wait(timeout=5)
            except Exception:
                self.process.kill()
        finally:
            self.log_handle.close()


def command_attempt(command: list[str], log_file: Path, scenario: str, attempt_id: int, kind: str,
                    timeout: float) -> dict:
    return {
        "kind": kind,
        "scenario_id": scenario,
        "attempt_id": attempt_id,
        "command": command,
        "log_file": str(log_file),
        "timeout_seconds": timeout,
    }


def run_command_with_attempts(command: list[str], log_file: Path, scenario: str, kind: str, timeout: float) -> list[dict]:
    attempts: list[dict] = []
    for attempt_id in (1, 2):
        attempt = command_attempt(command, log_file, scenario, attempt_id, kind, timeout)
        attempts.append(attempt)
        with log_file.open("w") as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            attempt["pid"] = process.pid
            try:
                exit_code = process.wait(timeout=timeout)
                attempt["exit_code"] = exit_code
                attempt["log_tail"] = tail_text(log_file)
                if exit_code == 0:
                    return attempts
                attempt["failure_class"] = "process_exit"
                raise RuntimeError(
                    f"{kind} exited with {exit_code}; log={log_file}; tail={attempt['log_tail']}"
                )
            except subprocess.TimeoutExpired as error:
                attempt["timeout_class"] = "fixture_timeout"
                attempt["log_tail"] = tail_text(log_file)
                process.terminate()
                try:
                    process.wait(timeout=5)
                except Exception:
                    process.kill()
                    process.wait(timeout=5)
                attempt["exit_code"] = process.returncode
                if attempt_id == 1:
                    continue
                raise RuntimeError(
                    f"{kind} timed out after {error.timeout}s; log={log_file}; tail={attempt['log_tail']}"
                )
    return attempts


def attach_attempts(result: dict, attempts: list[dict]) -> dict:
    result["attempts"] = attempts
    result["flaky_attempts"] = max(0, len(attempts) - 1)
    return result


def start_listener(binary: Path, implementation: str, work: Path, scenario: Optional[str] = None,
                   result_file: Optional[Path] = None, seed_file: Optional[Path] = None,
                   expected_messages: Optional[int] = None, transport: str = "quic") -> Listener:
    ready_file = work / f"{implementation}-ready.json"
    stop_file = work / f"{implementation}.stop"
    log_file = work / f"{implementation}.log"
    store_dir = work / f"{implementation}-store"
    command = [
        str(binary),
        "listen",
        "--ready-file",
        str(ready_file),
        "--stop-file",
        str(stop_file),
        "--store-dir",
        str(store_dir),
        "--features",
        "ping,identify,autonatv2,relay,dcutr,dht,rendezvous,pubsub",
        "--transport",
        transport,
    ]
    if scenario is not None:
        command.extend(["--scenario", scenario])
    if result_file is not None:
        command.extend(["--result-file", str(result_file)])
    if seed_file is not None:
        command.extend(["--seed-file", str(seed_file)])
    if expected_messages is not None:
        command.extend(["--expected-messages", str(expected_messages)])
    log = log_file.open("w")
    process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
    try:
        ready = wait_json(ready_file, 20)
    except Exception as error:
        process.terminate()
        process.wait(timeout=5)
        log.close()
        raise RuntimeError(f"{implementation} listener did not become ready: {error}; log={log_file}; tail={tail_text(log_file)}")
    return Listener(process, ready, stop_file, log_file, log, command)


def start_destination(binary: Path, implementation: str, relay_addr: str, relay_peer_id: str, work: Path) -> Listener:
    ready_file = work / f"{implementation}-destination-ready.json"
    stop_file = work / f"{implementation}-destination.stop"
    log_file = work / f"{implementation}-destination.log"
    store_dir = work / f"{implementation}-destination-store"
    command = [
        str(binary),
        "destination",
        "--ready-file",
        str(ready_file),
        "--stop-file",
        str(stop_file),
        "--relay-addr",
        relay_addr,
        "--relay-peer-id",
        relay_peer_id,
        "--store-dir",
        str(store_dir),
    ]
    log = log_file.open("w")
    process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
    try:
        ready = wait_json(ready_file, 30)
    except Exception as error:
        process.terminate()
        process.wait(timeout=5)
        log.close()
        raise RuntimeError(f"{implementation} destination did not become ready: {error}; log={log_file}; tail={tail_text(log_file)}")
    return Listener(process, ready, stop_file, log_file, log, command)


def run_dial(binary: Path, implementation: str, scenario: str, peer_id: str, addr: str, work: Path,
             payload: Optional[str] = None, transport: str = "quic") -> dict:
    payload_suffix = "" if payload is None else f"-{payload}"
    result_file = work / f"{implementation}-dial-{scenario}{payload_suffix}.json"
    log_file = work / f"{implementation}-dial-{scenario}{payload_suffix}.log"
    store_dir = work / f"{implementation}-dial-{scenario}{payload_suffix}-store"
    command = [
        str(binary),
        "dial",
        "--scenario",
        scenario,
        "--peer-id",
        peer_id,
        "--addr",
        addr,
        "--result-file",
        str(result_file),
        "--store-dir",
        str(store_dir),
        "--transport",
        transport,
    ]
    if payload is not None:
        command.extend(["--payload", payload])
    try:
        attempts = run_command_with_attempts(command, log_file, scenario, "dial", DIAL_TIMEOUT_SECONDS)
    except RuntimeError as error:
        detail = str(error)
        if result_file.exists():
            detail += f"; result={result_file.read_text(errors='replace')}"
        raise RuntimeError(detail)
    return attach_attempts(json.loads(result_file.read_text()), attempts)


def run_pubsub_mixed_mesh_stress(binaries: dict[str, Path], root: Path) -> dict:
    work = root / PUBSUB_STRESS_SCENARIO
    work.mkdir(parents=True, exist_ok=True)
    seed_file = work / "mesh-seeds.txt"
    expected_messages = 3
    participants = [
        ("fcl", "fcl0"),
        ("fcl", "fcl1"),
        ("fcl", "fcl2"),
        ("fcl", "fcl3"),
        ("go", "go0"),
        ("go", "go1"),
        ("go", "go2"),
        ("rust", "rust0"),
        ("rust", "rust1"),
        ("rust", "rust2"),
    ]
    listeners: list[Listener] = []
    result_files: list[tuple[str, Path]] = []
    try:
        for implementation, name in participants:
            result_file = work / f"{name}-stress-result.json"
            listeners.append(
                start_listener(
                    binaries[implementation],
                    name,
                    work,
                    PUBSUB_STRESS_SCENARIO,
                    result_file,
                    seed_file,
                    expected_messages,
                )
            )
            result_files.append((name, result_file))

        seed_file.write_text(
            "\n".join(listener.ready["listen_addrs"][0] for listener in listeners) + "\n"
        )
        time.sleep(8)

        publishers = [
            ("fcl", "stress-fcl", "fcl0"),
            ("go", "stress-go", "go0"),
            ("rust", "stress-rust", "rust0"),
        ]
        listeners_by_name = {name: listener for (_, name), listener in zip(participants, listeners)}
        publish_results = []
        for implementation, payload, target_name in publishers:
            target = listeners_by_name[target_name]
            publish_results.append(
                run_dial(
                    binaries[implementation],
                    f"{implementation}-stress-publisher",
                    PUBSUB_STRESS_SCENARIO,
                    target.ready["peer_id"],
                    target.ready["listen_addrs"][0],
                    work,
                    payload,
                )
            )

        time.sleep(8)
    finally:
        for listener in listeners:
            listener.close()

    listener_results = []
    for name, result_file in result_files:
        result = wait_json(result_file, 10)
        result["name"] = name
        listener_results.append(result)
        if result.get("status") != "ok":
            raise RuntimeError(f"{name} stress listener reported {result}")

    return {
        "implementation": "mixed",
        "scenario": PUBSUB_STRESS_SCENARIO,
        "participants": [
            {
                "name": name,
                "implementation": implementation,
                "peer_id": listener.ready["peer_id"],
                "listen_addr": listener.ready["listen_addrs"][0],
            }
            for (implementation, name), listener in zip(participants, listeners)
        ],
        "publishers": publish_results,
        "listeners": listener_results,
    }


def run_relay_dial(binary: Path, implementation: str, scenario: str, target_peer_id: str, relay_peer_id: str,
                   relay_addr: str, work: Path) -> dict:
    result_file = work / f"{implementation}-relay-dial-{scenario}.json"
    log_file = work / f"{implementation}-relay-dial-{scenario}.log"
    store_dir = work / f"{implementation}-relay-dial-{scenario}-store"
    command = [
        str(binary),
        "dial-relay",
        "--scenario",
        scenario,
        "--peer-id",
        target_peer_id,
        "--relay-peer-id",
        relay_peer_id,
        "--relay-addr",
        relay_addr,
        "--result-file",
        str(result_file),
        "--store-dir",
        str(store_dir),
    ]
    try:
        attempts = run_command_with_attempts(command, log_file, scenario, "relay_dial", 60)
    except RuntimeError as error:
        detail = str(error)
        if result_file.exists():
            detail += f"; result={result_file.read_text(errors='replace')}"
        raise RuntimeError(detail)
    return attach_attempts(json.loads(result_file.read_text()), attempts)


def prepare_go_fixture(source_dir: Path, build_dir: Path, donors_root: Path) -> Path:
    work = build_dir / "go_fixture"
    work.mkdir(parents=True, exist_ok=True)
    (work / "main.go").write_text((source_dir / "go_fixture" / "main.go").read_text())
    (work / "go.mod").write_text(
        "module fcl-libp2p-go-fixture\n\n"
        "go 1.24\n\n"
        "require (\n"
        "\tgithub.com/libp2p/go-libp2p v0.0.0\n"
        "\tgithub.com/libp2p/go-libp2p-kad-dht v0.0.0\n"
        "\tgithub.com/libp2p/go-libp2p-pubsub v0.0.0\n"
        ")\n\n"
        f"replace github.com/libp2p/go-libp2p => {donors_root / 'go-libp2p'}\n"
        f"replace github.com/libp2p/go-libp2p-kad-dht => {donors_root / 'go-libp2p-kad-dht'}\n"
        f"replace github.com/libp2p/go-libp2p-pubsub => {donors_root / 'go-libp2p-pubsub'}\n"
    )
    binary = work / "go_fixture"
    if binary.exists():
        binary.unlink()
    run(["go", "mod", "tidy"], cwd=work)
    run(["go", "build", "-o", str(binary), "."], cwd=work)
    return binary


def prepare_rust_fixture(source_dir: Path, build_dir: Path, donors_root: Path) -> Path:
    work = build_dir / "rust_fixture"
    src = work / "src"
    src.mkdir(parents=True, exist_ok=True)
    (src / "main.rs").write_text((source_dir / "rust_fixture" / "main.rs").read_text())
    (work / "Cargo.toml").write_text(
        "[package]\n"
        "name = \"fcl-libp2p-rust-fixture\"\n"
        "version = \"0.1.0\"\n"
        "edition = \"2024\"\n\n"
        "[dependencies]\n"
        f"libp2p = {{ path = \"{donors_root / 'rust-libp2p' / 'libp2p'}\", features = [\"tokio\", \"tcp\", \"dns\", \"noise\", \"tls\", \"yamux\", \"quic\", \"ping\", \"identify\", \"autonat\", \"relay\", \"dcutr\", \"kad\", \"rendezvous\", \"gossipsub\", \"macros\"] }}\n"
        f"libp2p-stream = {{ path = \"{donors_root / 'rust-libp2p' / 'protocols' / 'stream'}\" }}\n"
        "futures = \"0.3\"\n"
        "rand = \"0.8.5\"\n"
        "serde_json = \"1\"\n"
        "tokio = { version = \"1\", features = [\"macros\", \"rt-multi-thread\", \"time\"] }\n"
    )
    run(["cargo", "build", "--release"], cwd=work)
    return work / "target" / "release" / "fcl-libp2p-rust-fixture"


def run_pair(dialer_binary: Path, dialer: str, listener_binary: Path, listener: str, scenario: str, root: Path) -> dict:
    return run_pair_with_transport(dialer_binary, dialer, listener_binary, listener, scenario, root, "quic")


def run_pair_with_transport(dialer_binary: Path, dialer: str, listener_binary: Path, listener: str, scenario: str,
                            root: Path, transport: str) -> dict:
    work = root / f"{transport}-{dialer}-to-{listener}-{scenario}"
    work.mkdir(parents=True, exist_ok=True)
    listener_result = work / f"{listener}-listen-{scenario}.json" if scenario in PUBSUB_SCENARIOS else None
    server = start_listener(listener_binary, listener, work, scenario, listener_result, transport=transport)
    try:
        addr = server.ready["listen_addrs"][0]
        peer_id = server.ready["peer_id"]
        result = run_dial(dialer_binary, dialer, scenario, peer_id, addr, work, transport=transport)
        delivered = wait_json(listener_result, 20) if listener_result is not None else None
        if delivered is not None and delivered.get("status") != "ok":
            raise RuntimeError(f"{listener} listener reported {delivered}")
        out = {
            "dialer": dialer,
            "listener": listener,
            "scenario": scenario,
            "transport": transport,
            "addr": addr,
            "selected_addresses": {
                "dial": addr,
                "listen": server.ready.get("listen_addrs", []),
            },
            "peer_id": peer_id,
            "listener_process": {
                "pid": server.process.pid,
                "command": server.command,
                "log_file": str(server.log_file),
            },
            "result": result,
            "listener_result": delivered,
        }
        if transport == "tcp":
            out["negotiated_security"] = "/noise"
            out["negotiated_muxer"] = "/yamux/1.0.0"
        elif transport == "tcp-tls":
            out["negotiated_security"] = "/tls/1.0.0"
            out["negotiated_muxer"] = "/yamux/1.0.0"
        return out
    finally:
        server.close()


def run_topology(binary: Path, implementation: str, scenario: str, root: Path) -> dict:
    work = root / f"{implementation}-{scenario}"
    work.mkdir(parents=True, exist_ok=True)
    result_file = work / "result.json"
    log_file = work / "topology.log"
    command = [
        str(binary),
        "topology",
        "--scenario",
        scenario,
        "--result-file",
        str(result_file),
        "--store-dir",
        str(work / "stores"),
    ]
    try:
        attempts = run_command_with_attempts(command, log_file, scenario, "topology", 60)
    except RuntimeError as error:
        detail = str(error)
        if result_file.exists():
            detail += f"; result={result_file.read_text(errors='replace')}"
        raise RuntimeError(detail)
    return {
        "implementation": implementation,
        "scenario": scenario,
        "result": attach_attempts(json.loads(result_file.read_text()), attempts),
    }


def run_native_relay_topology(binaries: dict[str, Path], source: str, relay_impl: str, destination_impl: str,
                              scenario: str, root: Path) -> dict:
    work = root / f"{source}-source-{relay_impl}-relay-{destination_impl}-destination-{scenario}"
    work.mkdir(parents=True, exist_ok=True)
    relay = start_listener(binaries[relay_impl], f"{relay_impl}-relay", work)
    destination = None
    try:
        relay_addr = relay.ready["listen_addrs"][0]
        destination = start_destination(
            binaries[destination_impl],
            destination_impl,
            relay_addr,
            relay.ready["peer_id"],
            work,
        )
        result = run_relay_dial(
            binaries[source],
            source,
            scenario,
            destination.ready["peer_id"],
            relay.ready["peer_id"],
            relay_addr,
            work,
        )
        return {
            "implementation": "mixed",
            "scenario": scenario,
            "source": source,
            "relay_impl": relay_impl,
            "destination_impl": destination_impl,
            "relay": relay.ready,
            "destination": destination.ready,
            "result": result,
        }
    finally:
        if destination is not None:
            destination.close()
        relay.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--enabled", required=True)
    parser.add_argument("--fcl-fixture", required=True)
    parser.add_argument("--source-dir", required=True)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--donors-root", required=True)
    args = parser.parse_args()

    if not enabled_from_args(args.enabled):
        print("SKIP: live libp2p interop disabled; configure FCL_ENABLE_LIBP2P_INTEROP=ON or set FCL_ENABLE_LIBP2P_INTEROP=1.")
        return 0

    donors_root = Path(args.donors_root).resolve()
    source_dir = Path(args.source_dir).resolve()
    build_dir = Path(args.build_dir).resolve()
    build_dir.mkdir(parents=True, exist_ok=True)

    require_tool("go")
    require_tool("cargo")
    require_donor(donors_root, "go-libp2p")
    require_donor(donors_root, "go-libp2p-kad-dht")
    require_donor(donors_root, "go-libp2p-pubsub")
    require_donor(donors_root, "rust-libp2p")
    require_donor(donors_root, "libp2p-specs")

    fcl_fixture = Path(args.fcl_fixture).resolve()
    go_fixture = prepare_go_fixture(source_dir, build_dir, donors_root)
    rust_fixture = prepare_rust_fixture(source_dir, build_dir, donors_root)

    binaries = {
        "fcl": fcl_fixture,
        "go": go_fixture,
        "rust": rust_fixture,
    }

    artifacts: list[dict] = []
    failures: list[str] = []
    root = build_dir / "interop-run"
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    for listener in ("go", "rust", "fcl"):
        for dialer in ("fcl", "go", "rust"):
            if listener == dialer:
                continue
            for scenario in SCENARIOS:
                try:
                    artifacts.append(run_pair(binaries[dialer], dialer, binaries[listener], listener, scenario, root))
                except Exception as error:
                    failures.append(f"{dialer}->{listener} {scenario}: {error}")
            for scenario in DHT_SCENARIOS:
                try:
                    artifacts.append(run_pair(binaries[dialer], dialer, binaries[listener], listener, scenario, root))
                except Exception as error:
                    failures.append(f"{dialer}->{listener} {scenario}: {error}")
            for scenario in PUBSUB_SCENARIOS:
                if "fcl" not in (dialer, listener):
                    continue
                try:
                    artifacts.append(run_pair(binaries[dialer], dialer, binaries[listener], listener, scenario, root))
                except Exception as error:
                    failures.append(f"{dialer}->{listener} {scenario}: {error}")
    for dialer, listener in (("fcl", "go"), ("go", "fcl"), ("fcl", "rust"), ("rust", "fcl")):
        for scenario in TCP_DIRECT_SCENARIOS:
            try:
                artifacts.append(
                    run_pair_with_transport(
                        binaries[dialer], dialer, binaries[listener], listener, scenario, root, "tcp"
                    )
                )
            except Exception as error:
                failures.append(f"{dialer}->{listener} tcp {scenario}: {error}")
            try:
                artifacts.append(
                    run_pair_with_transport(
                        binaries[dialer], dialer, binaries[listener], listener, scenario, root, "tcp-tls"
                    )
                )
            except Exception as error:
                failures.append(f"{dialer}->{listener} tcp-tls {scenario}: {error}")
    try:
        artifacts.append(run_pubsub_mixed_mesh_stress(binaries, root))
    except Exception as error:
        failures.append(f"{PUBSUB_STRESS_SCENARIO}: {error}")
    for listener, dialer in (("rust", "fcl"), ("fcl", "rust")):
        for scenario in RENDEZVOUS_SCENARIOS:
            try:
                artifacts.append(run_pair(binaries[dialer], dialer, binaries[listener], listener, scenario, root))
            except Exception as error:
                failures.append(f"{dialer}->{listener} {scenario}: {error}")
    for scenario in TOPOLOGY_SCENARIOS:
        try:
            artifacts.append(run_topology(binaries["fcl"], "fcl", scenario, root))
        except Exception as error:
            failures.append(f"fcl topology {scenario}: {error}")
        for source, relay_impl, destination_impl in NATIVE_TOPOLOGIES:
            try:
                artifacts.append(run_native_relay_topology(binaries, source, relay_impl, destination_impl, scenario, root))
            except Exception as error:
                failures.append(
                    f"{source}->{relay_impl}->{destination_impl} native relay topology {scenario}: {error}"
                )

    artifact_path = build_dir / "interop-artifacts.json"
    artifact_path.write_text(
        json.dumps(
            {
                "artifact_root": str(root),
                "artifacts": artifacts,
                "failures": failures,
            },
            indent=2,
        ) +
        "\n"
    )

    if failures:
        for failure in failures:
            print(f"INTEROP FAILURE: {failure}", file=sys.stderr)
        print(f"interop artifacts: {build_dir / 'interop-artifacts.json'}", file=sys.stderr)
        return 1

    print(f"live libp2p interop ok: {len(artifacts)} scenarios")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
