#!/usr/bin/env python3
"""Deterministic CSM firmware source snapshot manifest.

Only inputs owned by the PlatformIO firmware project are included. Generated
build/output trees and Python bytecode are deliberately excluded so a rebuild
cannot change the source identity.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


MANIFEST_SCHEMA = 1
SOURCE_ROOTS = ("include", "lib", "src", "tools")
SOURCE_FILES = ("platformio.ini",)
EXCLUDED_DIRS = frozenset((".pio", "artifacts", "__pycache__"))
EXCLUDED_SUFFIXES = frozenset((".pyc", ".pyo"))


def _is_excluded(relative_path: Path) -> bool:
    return bool(EXCLUDED_DIRS.intersection(relative_path.parts)) or (
        relative_path.suffix.lower() in EXCLUDED_SUFFIXES
    )


def source_input_paths(project_dir: Path) -> tuple[Path, ...]:
    """Return canonical project-relative build input paths in stable order."""
    root = Path(project_dir).resolve()
    relative_paths: set[Path] = set()

    for relative_name in SOURCE_FILES:
        path = root / relative_name
        if path.is_file():
            relative_paths.add(Path(relative_name))

    for relative_root in SOURCE_ROOTS:
        directory = root / relative_root
        if not directory.is_dir():
            continue
        for path in directory.rglob("*"):
            if not path.is_file():
                continue
            relative_path = path.relative_to(root)
            if not _is_excluded(relative_path):
                relative_paths.add(relative_path)

    return tuple(sorted(relative_paths, key=lambda value: value.as_posix()))


def build_source_manifest(project_dir: Path) -> dict:
    """Build a content-only manifest and deterministic aggregate SHA-256."""
    root = Path(project_dir).resolve()
    entries = []
    aggregate = hashlib.sha256()
    aggregate.update(b"CSM_SOURCE_MANIFEST_V1\0")

    for relative_path in source_input_paths(root):
        path_bytes = relative_path.as_posix().encode("utf-8")
        contents = (root / relative_path).read_bytes()
        file_digest = hashlib.sha256(contents).digest()

        aggregate.update(len(path_bytes).to_bytes(4, "big"))
        aggregate.update(path_bytes)
        aggregate.update(len(contents).to_bytes(8, "big"))
        aggregate.update(file_digest)
        entries.append(
            {
                "path": relative_path.as_posix(),
                "size": len(contents),
                "sha256": file_digest.hex(),
            }
        )

    source_hash = aggregate.hexdigest()
    return {
        "schema": MANIFEST_SCHEMA,
        "source_hash": source_hash,
        "source_id32": int(source_hash[:8], 16),
        "source_id64": int(source_hash[:16], 16),
        "files": entries,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--project-dir",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    parser.add_argument("--json", action="store_true", help="print full manifest")
    parser.add_argument("--output", type=Path, help="write full manifest JSON")
    args = parser.parse_args()

    manifest = build_source_manifest(args.project_dir)
    rendered = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8", newline="\n")
    if args.json:
        print(rendered, end="")
    else:
        print(manifest["source_hash"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
