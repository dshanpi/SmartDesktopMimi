#!/usr/bin/env python3
"""Generate deterministic SPDX and CycloneDX component inventories."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import uuid


ROOT = Path(__file__).resolve().parents[1]


def revision() -> str:
    override = os.environ.get("SOURCE_REVISION")
    if override is not None:
        if not all(character in "0123456789abcdef" for character in override) or len(override) not in (40, 64):
            raise SystemExit("SOURCE_REVISION must be a 40- or 64-character lowercase hex digest")
        return override
    return subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, check=True,
        stdout=subprocess.PIPE, text=True
    ).stdout.strip()


def source_timestamp() -> str:
    value = os.environ.get("SOURCE_DATE_EPOCH")
    if value is None:
        value = subprocess.run(
            ["git", "show", "-s", "--format=%ct", "HEAD"],
            cwd=ROOT,
            check=True,
            stdout=subprocess.PIPE,
            text=True,
        ).stdout.strip()
    try:
        epoch = int(value)
    except ValueError as exc:
        raise SystemExit("SOURCE_DATE_EPOCH must be an integer") from exc
    if epoch < 0:
        raise SystemExit("SOURCE_DATE_EPOCH must be non-negative")
    return datetime.fromtimestamp(epoch, timezone.utc).replace(
        microsecond=0,
    ).isoformat().replace("+00:00", "Z")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = json.loads((ROOT / "third_party/components.json").read_text(encoding="utf-8"))
    components = source["components"]
    release_profile = json.loads(
        (ROOT / "docs/public-release.json").read_text(encoding="utf-8")
    )
    binary_assets = release_profile["redistributable_files"]
    stamp = source_timestamp()
    namespace = f"https://09make.inc/sbom/aitvbox/{revision()}"
    args.output.mkdir(parents=True, exist_ok=True)
    spdx = {
        "spdxVersion": "SPDX-2.3", "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT", "name": "AI-DeskTopBox",
        "documentNamespace": namespace,
        "creationInfo": {"created": stamp, "creators": ["Organization: 09make.inc"]},
        "packages": [],
    }
    for index, item in enumerate(components, 1):
        spdx["packages"].append({
            "name": item["name"],
            "SPDXID": f"SPDXRef-Package-{index}",
            "versionInfo": item["revision"],
            "downloadLocation": item["source"],
            "filesAnalyzed": False,
            "licenseConcluded": item["license"],
            "licenseDeclared": item["license"],
            "comment": (
                f"path={item['path']}; license_file={item['license_file']}; "
                f"distribution={item['distribution']}"
            ),
        })
    for index, item in enumerate(binary_assets, len(components) + 1):
        spdx["packages"].append({
            "name": f"{item['component']} ({Path(item['path']).name})",
            "SPDXID": f"SPDXRef-Package-{index}",
            "downloadLocation": item["source"],
            "filesAnalyzed": False,
            "licenseConcluded": item["license"],
            "licenseDeclared": item["license"],
            "checksums": [{"algorithm": "SHA256", "checksumValue": item["sha256"]}],
            "comment": f"path={item['path']}; license_file={item['license_file']}",
        })
    cyclonedx = {
        "bomFormat": "CycloneDX", "specVersion": "1.5", "version": 1,
        "serialNumber": f"urn:uuid:{uuid.uuid5(uuid.NAMESPACE_URL, namespace)}",
        "metadata": {"timestamp": stamp, "component": {"type": "application", "name": "AI-DeskTopBox", "version": revision()}},
        "components": [],
    }
    for item in components:
        component = {
            "type": "library", "name": item["name"],
            "version": item["revision"], "bom-ref": f"path:{item['path']}",
            "licenses": [{"expression": item["license"]}],
            "properties": [
                {"name": "09make:distribution", "value": item["distribution"]},
                {"name": "09make:license-file", "value": item["license_file"]},
            ],
        }
        if item["source"] != "NOASSERTION":
            component["externalReferences"] = [
                {"type": "vcs", "url": item["source"]},
            ]
        cyclonedx["components"].append(component)
    for item in binary_assets:
        cyclonedx["components"].append({
            "type": "file", "name": Path(item["path"]).name,
            "bom-ref": f"path:{item['path']}",
            "hashes": [{"alg": "SHA-256", "content": item["sha256"]}],
            "licenses": [{"expression": item["license"]}],
            "externalReferences": [{"type": "distribution", "url": item["source"]}],
            "properties": [
                {"name": "09make:component", "value": item["component"]},
                {"name": "09make:license-file", "value": item["license_file"]},
            ],
        })
    (args.output / "aitvbox.spdx.json").write_text(json.dumps(spdx, indent=2) + "\n", encoding="utf-8")
    (args.output / "aitvbox.cdx.json").write_text(json.dumps(cyclonedx, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
