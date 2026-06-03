#!/usr/bin/env python3
import json
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_donor_matrix.py donor_cases.json", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    data = json.loads(path.read_text())
    allowed = set(data.get("allowed_statuses", []))
    errors: list[str] = []

    seen: set[str] = set()
    for case in data.get("cases", []):
        case_id = case.get("id", "")
        status = case.get("status", "")
        supported = bool(case.get("supported", False))
        tests = case.get("fcl_tests", [])

        if not case_id:
            errors.append("case without id")
            continue
        if case_id in seen:
            errors.append(f"duplicate case id: {case_id}")
        seen.add(case_id)
        if status not in allowed:
            errors.append(f"{case_id}: unknown status {status!r}")
        if supported and status == "unsupported":
            errors.append(f"{case_id}: supported behavior must not be marked unsupported")
        if supported and not tests:
            errors.append(f"{case_id}: supported behavior must list at least one FCL test")

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print(f"donor matrix ok: {len(seen)} cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
