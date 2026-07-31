Import("env")

import hashlib
import json
from pathlib import Path


PRODUCT_ENVIRONMENTS = {
    "portenta_h7_m7_mid_feeder_uart_j4_remote_product_wifi",
    "portenta_h7_m7_mid_feeder_uart_j4_remote_service_hil_wifi",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


environment_name = env.subst("$PIOENV")
if environment_name in PRODUCT_ENVIRONMENTS:
    project_dir = Path(env.subst("$PROJECT_DIR"))
    artifact_dir = (
        project_dir
        / "third_party"
        / "mbed_portenta_product"
        / "artifact"
        / "PORTENTA_H7_M7"
    )
    manifest_path = artifact_dir / "artifact-manifest.json"
    if not manifest_path.is_file():
        raise RuntimeError(
            "Pinned product Mbed artifact is missing; run tools/build_pinned_mbed.ps1"
        )
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    library = artifact_dir / "libmbed.a"
    config = artifact_dir / "mbed_config.h"
    overrides = artifact_dir / "csm_product_mbed_overrides.h"
    expected_library = manifest["output"]["libmbed_sha256"].upper()
    expected_config = manifest["output"]["mbed_config_sha256"].upper()
    expected_overrides = manifest["output"]["app_overrides_sha256"].upper()
    if (
        sha256(library) != expected_library
        or sha256(config) != expected_config
        or sha256(overrides) != expected_overrides
    ):
        raise RuntimeError("Pinned product Mbed artifact hash mismatch")

    # -lmbed resolves against this directory before the stock framework.
    # Application/core compilation keeps Arduino's complete target ABI and
    # force-includes only the measured product network overrides.
    env.Prepend(LIBPATH=[str(artifact_dir)])
    env.Append(CCFLAGS=["-include", str(overrides)])
    print(
        "CSM product Mbed overlay: "
        f"{expected_library[:12]} / config {expected_config[:12]}"
        f" / overrides {expected_overrides[:12]}"
    )
