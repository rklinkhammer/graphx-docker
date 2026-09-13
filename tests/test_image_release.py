#!/usr/bin/env python3
"""Offline image boundary tests; synthetic OCI bytes never count as release pins."""

import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from unittest.mock import patch
from pathlib import Path

ROOT = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(ROOT / "scripts/release"))
from image_release import (LAYER, MANIFEST, RECIPES, canonical_image, catalog_evidence, digest, encoded,
                           image_sbom, inspect_image, pin_catalog, smoke_image, verify)
from release_common import ReleaseError, sha256_file

COMMIT = "a" * 40


def tar_bytes(files, timestamp=0):
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w") as archive:
        for name, data in files.items():
            member = tarfile.TarInfo(name)
            member.mode = 0o755
            member.mtime = timestamp
            member.size = len(data)
            archive.addfile(member, io.BytesIO(data))
    return buffer.getvalue()


def image(role, *, platform="arm64", user="65532:65532", extra=None, corrupt=False, timestamp=0):
    files = {"usr/local/bin/" + name: b"test executable" for name in RECIPES[role][1]}
    files.update({"lib/apk/db/installed": b"P:libc\nV:1.0\nL:MIT\n"})
    if role == "runtime":
        files["usr/local/share/graphx/build-dependencies.json"] = b'{"yamlCpp":"0.9.0","openssl":"3.0.0"}'
    if role == "telemetry":
        files.update({name: b"{}" for name in ("app/server.mjs", "app/web/dist/index.html",
                                               "config/schema/normalized-graph.schema.json")})
        files["app/node_modules/ws/package.json"] = b'{"name":"ws","version":"1.0"}'
        files["usr/local/share/graphx/web-package-lock.json"] = b'{"packages":{}}'
    files.update(extra or {})
    layer = tar_bytes(files, timestamp)
    config = encoded({"os": "linux", "architecture": platform,
                      "rootfs": {"diff_ids": [digest(layer)]},
                      "config": {"User": user, "Labels": {"org.opencontainers.image.version": "1.1.0",
                                 "org.opencontainers.image.revision": COMMIT},
                                 "Cmd": {"runtime": ["/usr/local/bin/graphx", "--help"],
                                         "telemetry": ["node", "server.mjs"],
                                         "sdr": ["/usr/local/bin/graphx-sdr", "--help"]}[role]}})
    def descriptor(data, media):
        return {"digest": digest(data), "size": len(data), "mediaType": media}
    manifest = encoded({"schemaVersion": 2, "config": descriptor(config, "application/vnd.oci.image.config.v1+json"),
                        "layers": [descriptor(layer, LAYER)]})
    return tar_bytes({"oci-layout": b'{"imageLayoutVersion":"1.0.0"}',
                      "index.json": encoded({"schemaVersion": 2, "manifests": [descriptor(manifest, MANIFEST)]}),
                      "blobs/sha256/" + digest(manifest)[7:]: manifest,
                      "blobs/sha256/" + digest(config)[7:]: config,
                      "blobs/sha256/" + digest(layer)[7:]: layer + (b"corrupt" if corrupt else b"")})


def rejected(action):
    try:
        action()
    except ReleaseError:
        return
    raise AssertionError("malformed release artifact accepted")


with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    manifest = {"version": 1, "release_version": "1.1.0", "commit": COMMIT,
                "source_date_epoch": 1700000000, "dirty_candidate": True,
                "platform": "linux/arm64", "images": {}}
    for role in RECIPES:
        path = root / (role + ".oci.tar")
        path.write_bytes(image(role))
        inspection = inspect_image(path, role, "1.1.0", COMMIT, "linux/arm64")
        assert inspection["digest"] != inspection["config_digest"]
        manifest["images"][role] = {"archive_sha256": sha256_file(path), "inspection": inspection}
        (root / (role + ".spdx.json")).write_bytes(encoded(image_sbom(role, inspection, "1.1.0", 1700000000)))
    pin_catalog(ROOT / "config/catalog", root / "catalog", manifest)
    manifest["catalog_sha256"] = sha256_file(root / "catalog/lock.json")
    (root / "images.json").write_bytes(encoded(manifest))
    # JSON round-tripping is part of the independent verifier boundary.
    verify(root)
    sbom = root / "sdr.spdx.json"
    saved = sbom.read_bytes()
    sbom.write_text("{}")
    rejected(lambda: verify(root))
    sbom.write_bytes(saved)
    rejected(lambda: pin_catalog(ROOT / "config/catalog", root / "catalog", manifest))
    source_type = json.loads((ROOT / "config/catalog/types/sample.source.json").read_bytes())
    pinned_type = json.loads((root / "catalog/types/sample.source.json").read_bytes())
    assert pinned_type["revision"] == source_type["revision"] + 1
    assert pinned_type["image"] == catalog_evidence(manifest)["images"]["runtime"]["image"]
    source_catalog = root / 'source-catalog'
    shutil.copytree(ROOT / 'config/catalog', source_catalog)
    type_file = source_catalog / 'types/sample.source.json'
    source_type['revision'] = 2147483647
    type_file.write_bytes(encoded(source_type))
    lock = json.loads((source_catalog / 'lock.json').read_bytes())
    next(entry for entry in lock['files'] if entry['path'] == 'types/sample.source.json')['sha256'] = sha256_file(type_file)
    (source_catalog / 'lock.json').write_bytes(encoded(lock))
    rejected(lambda: pin_catalog(source_catalog, root / 'overflow-catalog', manifest))
    assert not (root / 'overflow-catalog').exists()
    (root / "catalog/types/sample.source.json").write_text("{}")
    rejected(lambda: verify(root))
    path = root / "invalid.tar"
    for options in ({"platform": "amd64"}, {"user": "0:0"}, {"corrupt": True},
                    {"extra": {"../escape": b"x"}},
                    {"extra": {"etc/graphx/graphx.yml": b"topology"}},
                    {"extra": {"root/private.pem": b"-----BEGIN RSA PRIVATE KEY-----\n" + b"A" * 32}},
                    {"extra": {"run/secrets/hmac": b"secret"}}):
        path.write_bytes(image("runtime", **options))
        rejected(lambda: inspect_image(path, "runtime", "1.1.0", COMMIT, "linux/arm64"))
    normalized = []
    for timestamp in (1, 2000000000):
        path.write_bytes(image("runtime", timestamp=timestamp))
        output = root / f"normalized-{timestamp}.tar"
        canonical_image(path, output, 1700000000)
        inspect_image(output, "runtime", "1.1.0", COMMIT, "linux/arm64")
        normalized.append(output.read_bytes())
    assert normalized[0] == normalized[1]

for path in ROOT.joinpath("examples").rglob("*compose*.yaml"):
    assert not any(line.strip() == "build:" for line in path.read_text().splitlines()), path
assert not (ROOT / "examples/sdr-node/Dockerfile.services").exists()
for role, (name, _) in RECIPES.items():
    text = (ROOT / name).read_text()
    assert "COPY examples/sample-pipeline" not in text
    assert all(line == "ENTRYPOINT []" for line in text.splitlines() if line.startswith("ENTRYPOINT"))
    for line in text.splitlines():
        if line.startswith("FROM "):
            assert "@sha256:" in line
print("OCI digest, layer, platform, credential, SBOM and catalog boundaries passed")

# A killed attached Docker client must not leave its owned smoke container alive.
calls = []
def fake_run(command, **kwargs):
    calls.append(command)
    if command[:2] == ['docker', 'start']:
        raise subprocess.TimeoutExpired(command, 30)
    return subprocess.CompletedProcess(command, 0)
with patch('image_release.subprocess.run', side_effect=fake_run), patch(
        'image_release.subprocess.check_output', return_value='b' * 64):
    try:
        smoke_image(Path('test-image.tar'), 'runtime', 'sha256:' + 'a' * 64)
    except subprocess.TimeoutExpired:
        pass
    else:
        raise AssertionError('smoke timeout was ignored')
assert ['docker', 'rm', '--force', 'b' * 64] in calls
assert not any(command[:3] == ['docker', 'image', 'rm'] for command in calls)

if len(sys.argv) == 4:
    cli, release = Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve()
    manifest = verify(release)
    allowed = {value["image"] for value in catalog_evidence(manifest)["images"].values()}
    cases = {"S01": "sample-pipeline", "T01": "variants/renamed-multi-source", "T02": "variants/multi-radio"}
    with tempfile.TemporaryDirectory(prefix="graphx-release-compile-") as temporary:
        root = Path(temporary).resolve()
        shutil.copytree(release / "catalog", root / "catalog")
        for identifier, name in cases.items():
            inputs = root / identifier
            inputs.mkdir()
            graph = inputs / "graphx.yml"
            graph.write_text(re.sub(r"(?m)^catalog:.*$", "catalog: ../catalog/lock.json",
                                    (ROOT / "examples" / name / "graphx.yml").read_text()))
            output = root / (identifier + "-compiled")
            subprocess.run([str(cli), "compile", str(graph), "--target", "orbstack", "--catalog-root",
                            str(root / "catalog"), "--source-root", str(ROOT), "--credential-root",
                            str(root / "credentials"), "--output", str(output)], check=True)
            compose = json.loads((output / "compose.yaml").read_bytes())
            assert all(value["image"] in allowed and "build" not in value for value in compose["services"].values())
            assert not list(output.rglob("*Dockerfile*"))
            subprocess.run(["docker", "compose", "--project-directory", str(output), "-f",
                            str(output / "compose.yaml"), "config", "--quiet"], check=True,
                           env={**os.environ, **{name: str(root / name) for name in
                                                ("GX_OUTPUT", "GX_STATE", "GX_CREDENTIALS", "GX_RELEASE")},
                                "GX_OWNER": "release-verification"})
            shutil.copytree(output, release / "compiled-examples" / identifier)
    print("S01/T01/T02 compiled with verified release pins; three Compose models; zero Dockerfiles")
