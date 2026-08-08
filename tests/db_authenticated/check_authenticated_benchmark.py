#!/usr/bin/env python3

import json
import pathlib
import subprocess
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


benchmark = pathlib.Path(sys.argv[1])
label = 'cli-"json"-check'
completed = subprocess.run(
    [
        str(benchmark),
        "--keys",
        "17",
        "--chunk-keys",
        "7",
        "--value-bytes",
        "8",
        "--mdbx-upper-bytes",
        "134217728",
        "--machine-label",
        label,
    ],
    capture_output=True,
    text=True,
    check=False,
)
require(completed.returncode == 0, f"benchmark smoke failed: {completed.stderr}")
payload = json.loads(completed.stdout)
require(payload["format"] == "forge.db.authenticated.benchmark.v3", "unexpected benchmark format")
require(payload["config"]["workload"] == "initial_bulk_state", "unexpected workload")
require(
    payload["config"]["measurement_scope"] == "transaction_join_stage_durable_commit",
    "unexpected timing scope",
)
require(payload["config"]["machine_label"] == label, "machine label was not JSON escaped")
require(payload["config"]["mdbx_upper_bytes"] == 134217728, "MDBX upper-size override was not applied")
require(payload["config"]["planned_committed_versions"] == 3, "wrong planned version count")
require(payload["root"]["version"] == 2, "wrong final root version")
require(payload["root"]["state_size"] == 17, "wrong final state size")
require(payload["storage"]["committed_versions"] == 3, "wrong committed version count")
require(payload["storage"]["retained_versions"] == 3, "wrong verified retained version count")
require(payload["storage"]["retention_content_checks"] == 8, "wrong retention content-check count")
require(payload["storage"]["retention_verification_elapsed_ms"] >= 0, "invalid verification elapsed time")
initial_batch = payload["metrics"]["initial_batch"]
require(initial_batch["elapsed_ms"] >= 0, "invalid initial-batch elapsed time")
require(initial_batch["excluded_mutation_construction_elapsed_ms"] >= 0, "invalid mutation construction time")
require(
    initial_batch["elapsed_ms"] + initial_batch["excluded_mutation_construction_elapsed_ms"]
    <= initial_batch["load_wall_elapsed_ms"] + 0.002,
    "initial-batch and mutation-construction timers overlap",
)
require(not pathlib.Path(payload["config"]["path"]).exists(), "temporary benchmark directory was not removed")

for arguments in (
    ["--chunk-keys=0"],
    ["--mdbx-upper-bytes=0"],
    ["--baseline=1m", "--keys=17"],
    ["--unexpected"],
):
    rejected = subprocess.run([str(benchmark), *arguments], capture_output=True, text=True, check=False)
    require(rejected.returncode == 1, f"invalid CLI was not rejected: {arguments}")
    require(not rejected.stdout, f"invalid CLI emitted JSON: {arguments}")
    require("usage: benchmark_forge_db_authenticated" in rejected.stderr, f"invalid CLI omitted usage: {arguments}")
