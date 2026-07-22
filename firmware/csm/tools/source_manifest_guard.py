#!/usr/bin/env python3
"""Guard deterministic CSM source snapshot coverage and exclusions."""

from __future__ import annotations

import hashlib
from pathlib import Path
import sys


IS_SCONS_SCRIPT = "Import" in globals()
if IS_SCONS_SCRIPT:
    Import("env")
    PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
    TOOLS_DIR = PROJECT_DIR / "tools"
else:
    PROJECT_DIR = Path(__file__).resolve().parents[1]
    TOOLS_DIR = PROJECT_DIR / "tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from source_manifest import (
    EXCLUDED_DIRS,
    SOURCE_FILES,
    SOURCE_ROOTS,
    build_source_manifest,
    source_input_paths,
)


def _independent_aggregate(manifest: dict) -> str:
    aggregate = hashlib.sha256()
    aggregate.update(b"CSM_SOURCE_MANIFEST_V1\0")
    for entry in manifest["files"]:
        path_bytes = entry["path"].encode("utf-8")
        aggregate.update(len(path_bytes).to_bytes(4, "big"))
        aggregate.update(path_bytes)
        aggregate.update(int(entry["size"]).to_bytes(8, "big"))
        aggregate.update(bytes.fromhex(entry["sha256"]))
    return aggregate.hexdigest()


def validate_source_manifest(project_dir: Path) -> tuple[list[str], dict]:
    root = Path(project_dir).resolve()
    errors: list[str] = []
    paths = source_input_paths(root)
    manifest = build_source_manifest(root)
    repeated = build_source_manifest(root)

    if manifest != repeated:
        errors.append("two unchanged source snapshots produced different manifests")
    if not paths:
        errors.append("source snapshot is empty")
    if tuple(entry["path"] for entry in manifest["files"]) != tuple(
        path.as_posix() for path in paths
    ):
        errors.append("manifest entries are not in canonical path order")

    for required_file in SOURCE_FILES:
        if Path(required_file) not in paths:
            errors.append(f"missing required config input: {required_file}")
    for required_root in SOURCE_ROOTS:
        if not any(path.parts and path.parts[0] == required_root for path in paths):
            errors.append(f"source root has no manifested input: {required_root}")
    for path in paths:
        excluded = EXCLUDED_DIRS.intersection(path.parts)
        if excluded:
            errors.append(
                f"generated directory leaked into source snapshot: {path.as_posix()}"
            )

    for entry in manifest["files"]:
        path = root / entry["path"]
        contents = path.read_bytes()
        if len(contents) != entry["size"]:
            errors.append(f"size mismatch: {entry['path']}")
        if hashlib.sha256(contents).hexdigest() != entry["sha256"]:
            errors.append(f"content hash mismatch: {entry['path']}")

    independent_hash = _independent_aggregate(manifest)
    if manifest["source_hash"] != independent_hash:
        errors.append("aggregate source SHA-256 does not match manifest entries")
    if manifest["source_id32"] != int(manifest["source_hash"][:8], 16):
        errors.append("SOURCE_ID32 is not the high 32 bits of source SHA-256")
    if manifest["source_id64"] != int(manifest["source_hash"][:16], 16):
        errors.append("SOURCE_ID64 is not the high 64 bits of source SHA-256")
    if len(manifest["source_hash"]) != 64:
        errors.append("source hash is not a full SHA-256 hex string")

    return errors, manifest


def run_guard(project_dir: Path) -> int:
    errors, manifest = validate_source_manifest(project_dir)
    if errors:
        print("Source manifest guard FAIL:")
        for error in errors:
            print(f"  - {error}")
        return 1
    print(
        "Source manifest guard PASS: "
        f"files={len(manifest['files'])} "
        f"sha256={manifest['source_hash']} "
        f"source_id32=0x{manifest['source_id32']:08X} "
        f"source_id64=0x{manifest['source_id64']:016X}"
    )
    return 0


if IS_SCONS_SCRIPT:
    result = run_guard(PROJECT_DIR)
    if result:
        Exit(result)
elif __name__ == "__main__":
    raise SystemExit(run_guard(PROJECT_DIR))
