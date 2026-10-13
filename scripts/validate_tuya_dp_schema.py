#!/usr/bin/env python3
"""Validate an exported/runtime Tuya DP schema against the AITVBox contract."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Any


REPO = Path(__file__).resolve().parents[1]
DEFAULT_EXPECTED = (
    REPO / "integrations/tuyaopen/config/aitvbox-tuya-dp-schema.json"
)


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read JSON {path}: {exc}") from exc


def extract_dps(document: Any) -> list[dict[str, Any]]:
    """Accept the repo manifest and common Tuya schema export envelopes."""
    value = document
    for _ in range(5):
        if isinstance(value, str):
            value = json.loads(value)
            continue
        if isinstance(value, list):
            if all(isinstance(item, dict) for item in value):
                return value
            break
        if not isinstance(value, dict):
            break
        for key in ("dps", "schema", "functions", "result"):
            if key in value:
                value = value[key]
                break
        else:
            break
    raise ValueError("JSON does not contain a DP list (dps/schema/functions/result)")


def normalize_property(dp: dict[str, Any]) -> dict[str, Any]:
    prop = dp.get("property", {})
    if isinstance(prop, str):
        prop = json.loads(prop)
    if not isinstance(prop, dict):
        raise ValueError(f"DP{dp.get('id', '?')} property is not an object")
    return prop


def compare(expected: list[dict[str, Any]], actual: list[dict[str, Any]]) -> list[str]:
    problems: list[str] = []
    actual_by_id: dict[int, dict[str, Any]] = {}
    for dp in actual:
        try:
            dpid = int(dp["id"])
        except (KeyError, TypeError, ValueError):
            problems.append("actual schema contains a DP without a numeric id")
            continue
        actual_by_id[dpid] = dp

    for wanted in expected:
        dpid = int(wanted["id"])
        got = actual_by_id.get(dpid)
        if got is None:
            problems.append(f"DP{dpid}: missing")
            continue

        for field in ("mode", "type"):
            if str(got.get(field, "")).lower() != str(wanted[field]).lower():
                problems.append(
                    f"DP{dpid}: {field}={got.get(field)!r}, expected {wanted[field]!r}"
                )

        wanted_prop = normalize_property(wanted)
        got_prop = normalize_property(got)
        for field in ("type", "min", "max", "step", "scale", "range"):
            if field not in wanted_prop:
                continue
            if got_prop.get(field) != wanted_prop[field]:
                problems.append(
                    f"DP{dpid}: property.{field}={got_prop.get(field)!r}, "
                    f"expected {wanted_prop[field]!r}"
                )
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("actual", type=Path, help="Tuya schema JSON to validate")
    parser.add_argument(
        "--expected", type=Path, default=DEFAULT_EXPECTED,
        help=f"expected contract (default: {DEFAULT_EXPECTED})",
    )
    args = parser.parse_args()

    try:
        expected = extract_dps(load_json(args.expected))
        actual = extract_dps(load_json(args.actual))
        problems = compare(expected, actual)
    except (ValueError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    if problems:
        print(f"FAIL: Tuya product schema matches {len(expected) - sum(p.endswith(': missing') for p in problems)}/{len(expected)} required DP IDs")
        for problem in problems:
            print(f"- {problem}")
        return 1

    print(f"PASS: Tuya product schema contains all {len(expected)} required DPs with compatible types, modes, and ranges")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
