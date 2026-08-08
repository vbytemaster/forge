#!/usr/bin/env python3

import json
import pathlib
import re
import subprocess
import sys


if len(sys.argv) != 3:
    raise SystemExit("usage: check_spring_api_manifest.py FORGE_ROOT (SPINE_ROOT | --manifest-only)")

root = pathlib.Path(sys.argv[1]).resolve()
manifest_only = sys.argv[2] == "--manifest-only"
spine_root = None if manifest_only else pathlib.Path(sys.argv[2]).resolve()
manifest_path = root / "tests/chain_api/spring_api_manifest.json"
manifest = json.loads(manifest_path.read_text())

expected_donor = {
    "repository": "https://github.com/AntelopeIO/spring",
    "commit": "e6a99f68b67abc4d89fe716755b2e1394a4991f7",
    "license": "MIT",
}
expected_spine = {"repository": "https://github.com/vbytemaster/blockchain"}
if (
    manifest.get("schema") != 3
    or manifest.get("donor") != expected_donor
    or manifest.get("spine") != expected_spine
):
    raise SystemExit("Spring API manifest donor baseline changed")


def git_output(repository_root, *arguments):
    result = subprocess.run(
        ["git", "-C", str(repository_root), *arguments],
        capture_output=True,
        check=False,
        text=True,
    )
    if result.returncode != 0:
        raise SystemExit(f"Spine root is not a readable Git checkout: {repository_root}")
    return result.stdout.strip()


def canonical_repository_url(value):
    canonical = value.strip().rstrip("/")
    if canonical.startswith("git@github.com:"):
        canonical = f"https://github.com/{canonical.removeprefix('git@github.com:')}"
    return canonical.removesuffix(".git")


spine_commit = None
if spine_root is not None:
    if spine_root == root:
        raise SystemExit("Spine root must be a separate checkout")
    spine_commit = git_output(spine_root, "rev-parse", "HEAD")
    if re.fullmatch(r"[0-9a-f]{40}", spine_commit) is None:
        raise SystemExit("Spine root HEAD is not a full Git commit")
    spine_repository = git_output(spine_root, "config", "--get", "remote.origin.url")
    if canonical_repository_url(spine_repository) != canonical_repository_url(expected_spine["repository"]):
        raise SystemExit("Spine root origin does not match the manifest repository")
    if git_output(spine_root, "status", "--porcelain=v1", "--untracked-files=all"):
        raise SystemExit("Spine root must be clean; uncommitted acceptance evidence is not reproducible")

expected_sources = {
    "chain_registration": (
        "plugins/chain_api_plugin/chain_api_plugin.cpp",
        "913f705ccea61c6fc9f5926c00dee2e14c03e4fdc7fa543f0db89544da0d8ab5",
    ),
    "producer_registration": (
        "plugins/producer_api_plugin/producer_api_plugin.cpp",
        "1e159c03859c5e7240dac74745cce646560215c1a815e41ee2d1abcdd9086eda",
    ),
    "chain_dto": (
        "plugins/chain_plugin/include/eosio/chain_plugin/chain_plugin.hpp",
        "d4a97d48235599407168d47e45e6b7ae25f22d394d476278f37ef762bd68b261",
    ),
    "chain_info_dto": (
        "plugins/chain_plugin/include/eosio/chain_plugin/get_info_db.hpp",
        "317154a6bf9fd0922cc3119db91cde1d7b848fa8334b43f905c1c4aebd611db6",
    ),
    "account_query_dto": (
        "plugins/chain_plugin/include/eosio/chain_plugin/account_query_db.hpp",
        "ec4f1b7d91252520581b3ccd628feabdb24a9af145a6d0a90a1c7d674b400eb5",
    ),
    "chain_behavior": (
        "plugins/chain_plugin/chain_plugin.cpp",
        "b21ae5df990c0fd046e76dc319f126e730616fe225ec1d6d619aa3c377c84d98",
    ),
    "account_query_behavior": (
        "plugins/chain_plugin/account_query_db.cpp",
        "9dea67be79ba94d4dd4dc823c34538a6ea6d51c413383b0656d8149ac0521b2c",
    ),
    "producer_dto": (
        "plugins/producer_plugin/include/eosio/producer_plugin/producer_plugin.hpp",
        "9f0aafb34ce972347af1be399efbbd3fb5d0dd9f1d2a62f312e9cab8254b63f4",
    ),
    "producer_behavior": (
        "plugins/producer_plugin/producer_plugin.cpp",
        "b7fa8b8cb4c27f407660625c59b4274776f27a40fd090848391a342cb69621f2",
    ),
    "snapshot_dto": (
        "libraries/chain/include/eosio/chain/snapshot_scheduler.hpp",
        "591324bb8333803cad536dbacd8d3c0b67411b2b79375f19d1d4c79900345391",
    ),
}
sources = manifest.get("sources")
if not isinstance(sources, dict) or set(sources) != set(expected_sources):
    raise SystemExit("Spring API manifest source catalog is not exact")
for source_id, (path, digest) in expected_sources.items():
    if sources[source_id] != {"path": path, "sha256": digest}:
        raise SystemExit(f"Spring API donor source changed: {source_id}")

expected_endpoints = {
    "chain": {
        "get_info",
        "get_activated_protocol_features",
        "get_block",
        "get_block_info",
        "get_block_header_state",
        "get_account",
        "get_code",
        "get_code_hash",
        "get_consensus_parameters",
        "get_abi",
        "get_raw_code_and_abi",
        "get_raw_abi",
        "get_finalizer_info",
        "get_table_rows",
        "get_table_by_scope",
        "get_currency_balance",
        "get_currency_stats",
        "get_producers",
        "get_producer_schedule",
        "get_scheduled_transactions",
        "get_required_keys",
        "get_transaction_id",
        "compute_transaction",
        "push_transaction",
        "push_transactions",
        "send_transaction",
        "send_transaction2",
        "push_block",
        "get_accounts_by_authorizers",
        "send_read_only_transaction",
        "get_raw_block",
        "get_block_header",
        "get_transaction_status",
    },
    "producer": {
        "paused",
        "get_runtime_options",
        "get_greylist",
        "get_whitelist_blacklist",
        "get_scheduled_protocol_feature_activations",
        "get_supported_protocol_features",
        "get_account_ram_corrections",
        "get_unapplied_transactions",
        "get_snapshot_requests",
        "pause",
        "pause_at_block",
        "resume",
        "update_runtime_options",
        "add_greylist_accounts",
        "remove_greylist_accounts",
        "set_whitelist_blacklist",
        "create_snapshot",
        "schedule_snapshot",
        "unschedule_snapshot",
        "get_integrity_hash",
        "schedule_protocol_feature_activations",
    },
}

endpoints = manifest.get("endpoints")
if not isinstance(endpoints, list):
    raise SystemExit("Spring API manifest endpoints must be a list")

actual_endpoints = {group: set() for group in expected_endpoints}
donor_keys = []
for endpoint in endpoints:
    group = endpoint.get("group")
    donor = endpoint.get("donor")
    if group not in actual_endpoints or not isinstance(donor, str):
        raise SystemExit(f"invalid Spring endpoint identity: {group}.{donor}")
    actual_endpoints[group].add(donor)
    donor_keys.append((group, donor))

if len(donor_keys) != len(set(donor_keys)):
    raise SystemExit("Spring API manifest contains duplicate donor endpoints")
for group, expected in expected_endpoints.items():
    missing = sorted(expected - actual_endpoints[group])
    unexpected = sorted(actual_endpoints[group] - expected)
    if missing or unexpected:
        raise SystemExit(
            f"Spring API endpoint set changed for {group}: missing={missing}, unexpected={unexpected}"
        )

allowed_mappings = {"direct", "projection", "client_projection", "local_utility"}
api_sources = {}
for api in ("info", "block", "state", "transaction", "submission", "admin"):
    path = root / f"libraries/chain/api/include/forge/chain/api/{api}.cppm"
    api_sources[api] = path.read_text()

expected_conditions = {
    ("chain", "get_accounts_by_authorizers"): {
        "kind": "runtime",
        "expression": "chain_api_plugin enabled and chain.account_queries_enabled()",
    },
    ("chain", "get_transaction_status"): {
        "kind": "runtime",
        "expression": "chain_api_plugin enabled and chain.transaction_finality_status_enabled()",
    },
}
default_conditions = {
    "chain": {"kind": "plugin", "expression": "chain_api_plugin is enabled"},
    "producer": {"kind": "plugin", "expression": "producer_api_plugin is enabled"},
}
registration_sources = {
    "chain": "chain_registration",
    "producer": "producer_registration",
}


def require_source_refs(endpoint, field):
    refs = endpoint.get(field)
    if not isinstance(refs, list) or not refs:
        raise SystemExit(f"missing {field} source for {endpoint['group']}.{endpoint['donor']}")
    for ref in refs:
        if not isinstance(ref, dict) or set(ref) != {"source", "symbol"}:
            raise SystemExit(f"invalid {field} source for {endpoint['group']}.{endpoint['donor']}")
        if ref["source"] not in sources or not isinstance(ref["symbol"], str) or not ref["symbol"].strip():
            raise SystemExit(f"invalid {field} source for {endpoint['group']}.{endpoint['donor']}")
    return refs


def acceptance_test_names(value, identity):
    if isinstance(value, str) and value.strip():
        return [value]
    if (
        isinstance(value, list)
        and value
        and all(isinstance(name, str) and name.strip() for name in value)
        and len(value) == len(set(value))
    ):
        return value
    raise SystemExit(f"invalid Spine acceptance test for {identity}")


pending = []
evidence_ids = set()
validated_evidence = 0
required_fields = {
    "group",
    "donor",
    "registration",
    "dto",
    "behavior",
    "donor_evidence",
    "condition",
    "forge",
    "spine_acceptance",
}
for endpoint in endpoints:
    identity = f"{endpoint['group']}.{endpoint['donor']}"
    if set(endpoint) != required_fields:
        raise SystemExit(f"invalid endpoint fields for {identity}")

    expected_registration = {
        "source": registration_sources[endpoint["group"]],
        "symbol": endpoint["donor"],
    }
    if endpoint["registration"] != expected_registration:
        raise SystemExit(f"invalid registration source for {identity}")

    require_source_refs(endpoint, "dto")
    behavior = require_source_refs(endpoint, "behavior")
    if not any(endpoint["donor"] in ref["symbol"] for ref in behavior):
        raise SystemExit(f"behavior symbol does not identify {identity}")

    donor_evidence = endpoint["donor_evidence"]
    if not isinstance(donor_evidence, dict):
        raise SystemExit(f"invalid donor evidence for {identity}")
    if donor_evidence.get("status") == "present":
        if set(donor_evidence) != {"status", "path", "id"}:
            raise SystemExit(f"incomplete donor evidence for {identity}")
        if not donor_evidence["path"].strip() or not donor_evidence["id"].strip():
            raise SystemExit(f"empty donor evidence for {identity}")
    elif donor_evidence.get("status") == "none":
        if set(donor_evidence) != {"status", "reason"} or not donor_evidence["reason"].strip():
            raise SystemExit(f"explicit donor evidence absence needs a reason for {identity}")
    else:
        raise SystemExit(f"donor evidence must be present or explicit none for {identity}")

    expected_condition = expected_conditions.get(
        (endpoint["group"], endpoint["donor"]), default_conditions[endpoint["group"]]
    )
    if endpoint["condition"] != expected_condition:
        raise SystemExit(f"invalid conditional enablement for {identity}")

    forge = endpoint["forge"]
    if not isinstance(forge, dict) or set(forge) != {"api", "method", "mapping"}:
        raise SystemExit(f"invalid Forge mapping for {identity}")
    if forge["mapping"] not in allowed_mappings:
        raise SystemExit(f"unsupported Forge mapping for {identity}: {forge['mapping']}")
    if forge["api"] == "protocol":
        if (forge["method"], forge["mapping"]) != ("transaction.id", "local_utility"):
            raise SystemExit(f"invalid local utility mapping for {identity}")
    elif forge["api"] not in api_sources:
        raise SystemExit(f"unknown Forge API for {identity}: {forge['api']}")
    elif re.search(rf"\b{re.escape(forge['method'])}\s*\(", api_sources[forge["api"]]) is None:
        raise SystemExit(f"missing Forge API method for {identity}: {forge['api']}.{forge['method']}")

    acceptance = endpoint["spine_acceptance"]
    if (
        not isinstance(acceptance, dict)
        or not isinstance(acceptance.get("id"), str)
        or not acceptance["id"].startswith(f"SPINE-{endpoint['group'].upper()}-")
    ):
        raise SystemExit(f"invalid Spine acceptance evidence for {identity}")
    if acceptance["id"] in evidence_ids:
        raise SystemExit(f"duplicate Spine acceptance evidence id: {acceptance['id']}")
    evidence_ids.add(acceptance["id"])
    if acceptance.get("status") == "present":
        if set(acceptance) != {"status", "id", "path", "test"}:
            raise SystemExit(f"incomplete Spine acceptance evidence for {identity}")
        evidence_path_value = acceptance["path"]
        if not isinstance(evidence_path_value, str) or not evidence_path_value.startswith("tests/"):
            raise SystemExit(f"invalid Spine acceptance test for {identity}")
        evidence_path = pathlib.PurePosixPath(evidence_path_value)
        if evidence_path.is_absolute() or ".." in evidence_path.parts:
            raise SystemExit(f"invalid Spine acceptance test for {identity}")
        test_names = acceptance_test_names(acceptance["test"], identity)

        evidence_root = root if forge["mapping"] == "local_utility" else spine_root
        if evidence_root is not None:
            source_path = evidence_root / pathlib.Path(*evidence_path.parts)
            if not source_path.is_file():
                raise SystemExit(f"missing acceptance source for {identity}: {source_path}")
            source = source_path.read_text()
            for test_name in test_names:
                declaration = re.compile(
                    rf"\bBOOST_(?:AUTO|FIXTURE)_TEST_CASE\s*\(\s*{re.escape(test_name)}(?:\s*,|\s*\))"
                )
                if declaration.search(source) is None:
                    raise SystemExit(
                        f"missing acceptance test case for {identity}: {source_path}:{test_name}"
                    )
                validated_evidence += 1
    elif acceptance.get("status") == "pending":
        if set(acceptance) != {"status", "id", "reason"} or not acceptance["reason"].strip():
            raise SystemExit(f"pending Spine acceptance needs a reason for {identity}")
        pending.append((identity, acceptance["id"], acceptance["reason"]))
    else:
        raise SystemExit(f"Spine acceptance must be present or pending for {identity}")

if pending:
    details = "\n".join(f"  {identity} [{evidence_id}]: {reason}" for identity, evidence_id, reason in pending)
    raise SystemExit(f"Spring API acceptance evidence is pending:\n{details}")

if manifest_only:
    print(
        "Spring API manifest structure verified; "
        "Spine acceptance NOT_RUN: --manifest-only does not validate downstream evidence"
    )
    raise SystemExit(125)

print(
    "Spring API donor manifest verified: "
    f"{len(expected_endpoints['chain'])} chain + {len(expected_endpoints['producer'])} producer; "
    f"{validated_evidence} acceptance test case references validated at Spine {spine_commit}"
)
