#!/usr/bin/env python3
"""Build and independently verify offline, digest-addressed shared OCI images.

Uses the selected Docker engine's image store, never a privileged builder or a
registry. Docker's OCI-compatible save format supplies actual manifest digests;
an image config ID is never substituted for an OCI manifest digest.
"""

from __future__ import annotations

import argparse
import copy
import datetime as dt
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import tarfile
import tempfile
import uuid
from pathlib import Path, PurePosixPath

from release_common import ReleaseError, sha256_file, source_version, validate_commit, validate_epoch

RECIPES = {
    "runtime": ("Dockerfile", ["graphx", "graphx-generator", "graphx-transform", "graphx-sink",
                              "graphx-udp-publisher", "graphx-udp-subscriber"]),
    "telemetry": ("Dockerfile", ["graphx", "graphx-platform"]),
    "sdr": ("Dockerfile", ["graphx", "graphx-sdr-radio", "graphx-sdr-processor", "graphx-sdr-sink"]),
}
RECIPES["vita"] = ("Dockerfile", ["graphx-vita-radio", "graphx-vita-processor",
                                    "graphx-vita-detector", "graphx-vita-recorder"])
DEFAULT_ROLES = {"runtime", "telemetry", "sdr"}
VRT_PIN = "dbe85d37155145842da60367af1c4beef8801b0c"
MAX_IMAGE = 2 * 1024**3
MAX_JSON = 4 * 1024**2
MAX_FILES = 100_000
MANIFEST = "application/vnd.oci.image.manifest.v1+json"
LAYER = "application/vnd.oci.image.layer.v1.tar"
# Public known-answer fixtures, checked against GnuTLS 3.7.9
# https://github.com/gnutls/gnutls/blob/3.7.9/lib/crypto-selftests-pk.c
# These exact PEM bytes are allowed only in libgnutls, never credential files.
PUBLIC_SELFTEST_KEYS = {
    "7c4c63ee462e0e700cd9e29c8e0f730b3f1b484c4abdd83f1e69fcd477c061fa",
    "91ea1699ff6b1a34b4a1d500a9c75a808441e47b9ea68da6fb0195e01ce1dc61",
    "a4d138d7ef9748464117b44fb9c0a4b5b85a1599a127d02690abaa96d03c16e6",
    "d039c8119a029ab9f9c83c04d67002d887b6bc6026c4264402ab27cdf24cf138",
    "ef237ea8db4f2ae9ee100e8ced96d29b5dceb0e6a948443e6b8a00b1791f9ec9",
    "fa0b06a72461ec0a963dcfccb8d5b61bd88a6074fc7271573bff68ab86b8c1af",
}
KEY_HEADER = re.compile(rb"-----BEGIN ([A-Z0-9 ]*PRIVATE KEY)-----\r?\n[A-Za-z0-9+/]{16,}")
KEY_PEM = re.compile(rb"-----BEGIN ([A-Z0-9 ]*PRIVATE KEY)-----\r?\n[A-Za-z0-9+/=\r\n]+-----END \1-----")


def scan_keys(data, name, final=False):
    for header in KEY_HEADER.finditer(data):
        key = KEY_PEM.match(data, header.start())
        if key is None and not final and header.start() >= len(data) - 16384:
            continue
        require(key is not None and PurePosixPath(name).name.startswith("libgnutls.so.") and
                hashlib.sha256(key[0]).hexdigest() in PUBLIC_SELFTEST_KEYS,
                "private key material found in image file " + name)


def encoded(value) -> bytes:
    return (json.dumps(value, sort_keys=True, indent=2) + "\n").encode()


def digest(data: bytes) -> str:
    return "sha256:" + hashlib.sha256(data).hexdigest()


def safe_name(name: str) -> str:
    path = PurePosixPath(name)
    if not name or path.is_absolute() or ".." in path.parts or "\\" in name:
        raise ReleaseError("unsafe image artifact path")
    return path.as_posix().removeprefix("./")


def require(condition, message: str) -> None:
    if not condition:
        raise ReleaseError(message)


def canonical_image(source: Path, destination: Path, epoch: int) -> None:
    """Repack verified Docker save bytes as a deterministic OCI image, offline.

    The resulting config and manifest have new, actual content digests. Preserve
    layer order, modes, owners, links and xattrs; normalize only archive metadata.
    No files are extracted onto the host.
    """
    with tarfile.open(source, "r:") as original, tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        index = json.load(original.extractfile("index.json"))
        manifest = json.load(original.extractfile("blobs/sha256/" + index["manifests"][0]["digest"][7:]))
        config = json.load(original.extractfile("blobs/sha256/" + manifest["config"]["digest"][7:]))

        def store(data, media):
            value = digest(data)
            path = root / "blobs/sha256" / value[7:]
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            return {"mediaType": media, "digest": value, "size": len(data)}

        layers = []
        for descriptor in manifest["layers"]:
            data = original.extractfile("blobs/sha256/" + descriptor["digest"][7:]).read()
            output = io.BytesIO()
            with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as layer, tarfile.open(
                    fileobj=output, mode="w", format=tarfile.PAX_FORMAT) as normalized:
                for item in layer:
                    member = copy.copy(item)
                    member.mtime, member.uname, member.gname = epoch, "", ""
                    member.pax_headers = {key: value for key, value in item.pax_headers.items()
                                          if key not in ("mtime", "atime", "ctime")}
                    normalized.addfile(member, layer.extractfile(item) if item.isfile() else None)
            layers.append(store(output.getvalue(), LAYER))
        config["rootfs"]["diff_ids"] = [layer["digest"] for layer in layers]
        manifest["config"] = store(encoded(config), "application/vnd.oci.image.config.v1+json")
        manifest["layers"] = layers
        manifest.pop("annotations", None)
        descriptor = store(encoded(manifest), MANIFEST)
        (root / "index.json").write_bytes(encoded({"schemaVersion": 2, "manifests": [descriptor]}))
        (root / "oci-layout").write_bytes(encoded({"imageLayoutVersion": "1.0.0"}))
        # Compatibility inventory lets Docker's classic image store import the
        # exact config/layers without inventing a registry RepoDigest.
        (root / "manifest.json").write_bytes(encoded([{
            "Config": "blobs/sha256/" + manifest["config"]["digest"][7:], "RepoTags": [],
            "Layers": ["blobs/sha256/" + layer["digest"][7:] for layer in layers]}]))
        with tarfile.open(destination, "w", format=tarfile.PAX_FORMAT) as archive:
            for path in sorted(root.rglob("*")):
                if path.is_file():
                    info = tarfile.TarInfo(path.relative_to(root).as_posix())
                    info.size, info.mode, info.mtime = path.stat().st_size, 0o644, epoch
                    with path.open("rb") as stream:
                        archive.addfile(info, stream)


def inspect_image(path: Path, role: str, version: str, commit: str, platform: str) -> dict:
    """Verify descriptors, every layer and the installed package inventory offline."""
    require(path.is_file() and not path.is_symlink() and path.stat().st_size <= MAX_IMAGE,
            "image archive is missing, symlinked or exceeds 2 GiB")
    with tarfile.open(path, "r:") as archive:
        members = {}
        for item in archive:
            name = safe_name(item.name)
            require(name not in members and (item.isfile() or item.isdir()),
                    "duplicate or non-regular OCI archive member")
            members[name] = item
            require(len(members) <= MAX_FILES, "too many OCI archive members")

        def read(name, maximum=MAX_JSON):
            item = members.get(safe_name(name))
            require(item is not None and item.isfile() and item.size <= maximum,
                    "missing or oversized OCI member")
            return archive.extractfile(item).read()

        def blob(descriptor, maximum=MAX_JSON):
            value = descriptor.get("digest", "")
            require(re.fullmatch(r"sha256:[a-f0-9]{64}", value), "invalid OCI digest")
            data = read("blobs/sha256/" + value[7:], maximum)
            require(digest(data) == value and len(data) == descriptor.get("size"),
                    "OCI descriptor digest or size mismatch")
            return data

        require(json.loads(read("oci-layout")) == {"imageLayoutVersion": "1.0.0"},
                "OCI layout 1.0.0 required; use a Docker version with OCI-compatible save")
        index = json.loads(read("index.json"))
        require(index.get("schemaVersion") == 2 and len(index.get("manifests", [])) == 1,
                "one platform image per offline archive required")
        descriptor = index["manifests"][0]
        require(descriptor.get("mediaType") == MANIFEST, "OCI image manifest required")
        manifest = json.loads(blob(descriptor))
        require(manifest.get("schemaVersion") == 2, "invalid image manifest")
        config = json.loads(blob(manifest["config"]))
        require(config.get("os") + "/" + config.get("architecture") == platform,
                "image platform mismatch")
        settings = config.get("config", {})
        labels = settings.get("Labels", {})
        require(labels.get("org.opencontainers.image.version") == version and
                labels.get("org.opencontainers.image.revision") == commit,
                "image release labels mismatch")
        require(settings.get("User") == "65532:65532" and not settings.get("Entrypoint"),
                "shared image requires UID 65532 and no implicit entrypoint")
        expected_cmd = {"runtime": ["/usr/local/bin/graphx", "--help"],
                        "telemetry": ["graphx-platform", "--help"],
                        "sdr": ["/usr/local/bin/graphx-sdr", "--help"],
                        "vita": ["/usr/local/bin/graphx", "--help"]}[role]
        require(settings.get("Cmd") == expected_cmd, "unexpected shared image command")
        require(not any(v.startswith("GRAPHX_CONFIG=") for v in settings.get("Env", [])),
                "image contains a default authored graph")
        files, captured = {}, {}
        diff_ids = config.get("rootfs", {}).get("diff_ids", [])
        require(len(diff_ids) == len(manifest.get("layers", [])) <= 128,
                "layer/rootfs inventory mismatch")
        for layer_index, layer in enumerate(manifest["layers"]):
            require(layer.get("mediaType") == LAYER, "uncompressed OCI save layers required")
            data = blob(layer, MAX_IMAGE)
            require(digest(data) == diff_ids[layer_index], "OCI rootfs layer mismatch")
            with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as contents:
                count = 0
                for item in contents:
                    count += 1
                    require(count <= MAX_FILES, "too many layer members")
                    name = safe_name(item.name)
                    base = PurePosixPath(name).name
                    parent = str(PurePosixPath(name).parent)
                    if base.startswith(".wh."):
                        target = parent + "/" + base[4:]
                        if base == ".wh..wh..opq":
                            target = parent
                        for existing in list(files):
                            if existing == target or existing.startswith(target + "/"):
                                files.pop(existing, None)
                                captured.pop(existing, None)
                        continue
                    require(name not in ("etc/graphx/graphx.yml", "share/graphx/graphx.yml") and
                            not name.startswith(("run/secrets/", "run/sdr-tls/")),
                            "graph or credentials found in an image layer")
                    if item.isfile():
                        stream = contents.extractfile(item)
                        checksum = hashlib.sha256()
                        buffer, previous = bytearray(), b""
                        capture = (name in ("var/lib/dpkg/status", "lib/apk/db/installed",
                                            "usr/local/share/graphx/build-dependencies.json",
                                            "usr/local/share/graphx/vita-dependencies.json",
                                            "usr/local/share/graphx/web-package-lock.json") or
                                   name.endswith("/package.json") and "/node_modules/" in name)
                        while chunk := stream.read(1024 * 1024):
                            scan_keys(previous + chunk, name)
                            previous = chunk[-16384:]
                            checksum.update(chunk)
                            if capture:
                                buffer.extend(chunk)
                                require(len(buffer) <= MAX_JSON, "oversized package metadata")
                        scan_keys(previous, name, final=True)
                        files[name] = {"sha256": checksum.hexdigest(), "mode": item.mode}
                        if capture:
                            captured[name] = bytes(buffer)
                    elif item.issym() or item.islnk():
                        files[name] = {"link": item.linkname, "mode": item.mode}
                    require(len(files) <= MAX_FILES, "image file inventory exceeds bound")
        for executable in RECIPES[role][1]:
            installed = files.get("usr/local/bin/" + executable)
            require(installed and installed["mode"] & 0o111, "missing installed " + executable)
        if role == "telemetry":
            require(all(name in files for name in ("app/server.mjs", "app/node-console.mjs", "app/web/dist/index.html",
                                                   "config/schema/normalized-graph.schema.json")),
                    "platform service or console missing")
        packages = {}
        for paragraph in captured.get("var/lib/dpkg/status", b"").decode().split("\n\n"):
            fields = dict(line.split(": ", 1) for line in paragraph.splitlines() if ": " in line)
            if fields.get("Status") == "install ok installed":
                packages[(fields["Package"], fields["Version"])] = "NOASSERTION"
        for paragraph in captured.get("lib/apk/db/installed", b"").decode().split("\n\n"):
            fields = dict(line.split(":", 1) for line in paragraph.splitlines() if ":" in line)
            if "P" in fields and "V" in fields:
                packages[(fields["P"], fields["V"])] = fields.get("L", "NOASSERTION")
        for name, data in captured.items():
            if name.endswith("/package.json"):
                package = json.loads(data)
                if isinstance(package.get("name"), str) and isinstance(package.get("version"), str):
                    packages[(package["name"], package["version"])] = "NOASSERTION"
        if role in RECIPES:
            metadata = captured.get("usr/local/share/graphx/build-dependencies.json")
            require(metadata is not None, "runtime build dependency metadata missing")
            dependencies = json.loads(metadata)
            require(dependencies.get("yamlCpp") == "0.9.0", "bundled yaml-cpp version mismatch")
            packages[("yaml-cpp", dependencies["yamlCpp"])] = "MIT"
            packages[("OpenSSL", dependencies["openssl"])] = "Apache-2.0"
        if role == "vita":
            dependencies = json.loads(captured.get("usr/local/share/graphx/vita-dependencies.json", b"{}"))
            pins = {dep["name"]: dep for dep in dependencies.get("dependencies", [])}
            require(set(pins) == {"vrt_framework", "SoapySDR"} and
                    pins["vrt_framework"]["revision"] == VRT_PIN and
                    pins["SoapySDR"]["revision"] == "1cf5a539a21414ff509ff7d0eedfc5fa8edb90c6",
                    "VITA dependency pin mismatch")
            for name, dep in pins.items():
                require("usr/local/share/doc/graphx/vita-licenses/" + name + ".txt" in files,
                        "missing VITA dependency license")
                packages[(name, dep["revision"])] = dep["license"]
            require("usr/local/share/graphx/vita-dependencies.spdx.json" in files,
                    "missing VITA dependency SBOM")
            require(any(name.startswith("usr/local/lib/libSoapySDR.so") for name in files),
                    "missing SoapySDR runtime")
        if role == "telemetry":
            metadata = captured.get("usr/local/share/graphx/web-package-lock.json")
            require(metadata is not None, "console production dependency lock missing")
            for name, package in json.loads(metadata)["packages"].items():
                if name and not package.get("dev", False):
                    packages[(package.get("name", name.rsplit("node_modules/", 1)[-1]),
                              package["version"])] = package.get("license", "NOASSERTION")
        # Language runtimes supplied by the pinned upstream image are not APK packages.
        for variable in settings.get("Env", []):
            for key, name in (("NODE_VERSION=", "node"), ("PYTHON_VERSION=", "python")):
                if variable.startswith(key):
                    packages[(name, variable[len(key):])] = "NOASSERTION"
        require(packages, "image has no installed package inventory")
        require(role != "telemetry" or any(name == "ws" for name, _ in packages),
                "telemetry image is missing ws")
        return {"digest": descriptor["digest"], "config_digest": manifest["config"]["digest"],
                "platform": platform, "packages": sorted([n, v, l] for (n, v), l in packages.items()),
                "files": {name: value for name, value in sorted(files.items())
                          if name.startswith(("usr/local/bin/graphx", "opt/graphx-sdr/", "app/",
                                              "usr/local/share/graphx/", "usr/local/share/doc/graphx/"))}}


def image_sbom(role, inspection, version, epoch):
    root = "SPDXRef-GraphX"
    packages = [{"SPDXID": root, "name": "graphx-" + role, "versionInfo": version,
                 "downloadLocation": "NOASSERTION", "filesAnalyzed": False,
                 "licenseConcluded": "MIT", "licenseDeclared": "MIT",
                 "checksums": [{"algorithm": "SHA256", "checksumValue": inspection["digest"][7:]}]}]
    relationships = [{"spdxElementId": "SPDXRef-DOCUMENT", "relationshipType": "DESCRIBES",
                      "relatedSpdxElement": root}]
    for index, (name, ver, license_value) in enumerate(inspection["packages"]):
        identifier = f"SPDXRef-Package-{index}"
        packages.append({"SPDXID": identifier, "name": name, "versionInfo": ver,
                         "downloadLocation": "NOASSERTION", "filesAnalyzed": False,
                         "licenseConcluded": "NOASSERTION", "licenseDeclared": license_value})
        relationships.append({"spdxElementId": root, "relationshipType": "DEPENDS_ON",
                              "relatedSpdxElement": identifier})
    return {"spdxVersion": "SPDX-2.3", "SPDXID": "SPDXRef-DOCUMENT", "dataLicense": "CC0-1.0",
            "name": "graphx-" + role,
            "documentNamespace": "https://graphx.invalid/spdx/" + inspection["digest"][7:],
            "creationInfo": {"creators": ["Tool: graphx-image-release/1"],
                             "created": dt.datetime.fromtimestamp(epoch, dt.timezone.utc).strftime(
                                 "%Y-%m-%dT%H:%M:%SZ")},
            "packages": packages, "relationships": relationships}


def verify(directory: Path) -> dict:
    manifest = json.loads((directory / "images.json").read_bytes())
    require(manifest.get("version") == 1 and set(manifest.get("images", {})) in (DEFAULT_ROLES, set(RECIPES)),
            "invalid shared image release manifest")
    version = manifest["release_version"]
    commit = validate_commit(manifest["commit"])
    epoch = validate_epoch(manifest["source_date_epoch"])
    for role, record in manifest["images"].items():
        archive = directory / (role + ".oci.tar")
        require(sha256_file(archive) == record["archive_sha256"], "image archive checksum mismatch")
        inspection = inspect_image(archive, role, version, commit, manifest["platform"])
        require(inspection == record["inspection"], "image content differs from release inventory")
        sbom = directory / (role + ".spdx.json")
        require(not sbom.is_symlink() and sbom.read_bytes() == encoded(image_sbom(role, inspection, version, epoch)),
                "SPDX inventory does not match image bytes")
    catalog = directory / "catalog"
    require(sha256_file(catalog / "lock.json") == manifest["catalog_sha256"], "release catalog digest mismatch")
    lock = json.loads((catalog / "lock.json").read_bytes())
    names = set()
    for entry in lock["files"]:
        name = safe_name(entry["path"])
        require(name not in names, "duplicate release catalog path")
        names.add(name)
        path = catalog / name
        require(not path.is_symlink() and path.resolve().is_relative_to(catalog.resolve()) and
                sha256_file(path) == entry["sha256"], "release catalog file digest mismatch")
    require(json.loads((catalog / "release-images.json").read_bytes()) == catalog_evidence(manifest),
            "catalog image evidence mismatch")
    return manifest


def catalog_evidence(manifest):
    return {"version": 1, "platform": manifest["platform"],
            "dirty_candidate": manifest["dirty_candidate"], "execution_available": False,
            "images": {role: {"image": "graphx-" + role + "@" + record["inspection"]["digest"],
                              "archive_sha256": record["archive_sha256"],
                              "config_digest": record["inspection"]["config_digest"]}
                       for role, record in manifest["images"].items()}}


def pin_catalog(source: Path, destination: Path, manifest: dict, vita: Path | None = None) -> None:
    """Derive the existing authoritative catalog; this is not a second graph manifest."""
    require(not destination.exists(), "catalog destination already exists")
    lock = json.loads((source / "lock.json").read_bytes())
    documents = {}
    for entry in lock["files"]:
        name = safe_name(entry["path"])
        path = source / name
        require(not path.is_symlink() and path.resolve().is_relative_to(source.resolve()) and
                name not in documents and sha256_file(path) == entry["sha256"], "source catalog digest mismatch")
        documents[name] = json.loads(path.read_bytes())
    if vita is not None:
        require("vita" in manifest["images"], "VITA catalog requires verified VITA image")
        for path in sorted((vita / "types").glob("*.json")):
            name = "types/" + path.name
            require(name not in documents, "duplicate VITA type")
            documents[name] = json.loads(path.read_bytes())
            lock["files"].append({"path": name, "sha256": ""})
        schemas = json.loads((vita / "wire-schemas.json").read_bytes())
        require(not (set(schemas) & set(documents["wire-schemas.json"])), "duplicate VITA schema")
        documents["wire-schemas.json"].update(schemas)
    for name, document in documents.items():
        if name == "platform.json":
            role = "telemetry"
        elif name.startswith("types/") and "container" in document.get("execution", []):
            candidates = [role for role, (_, names) in RECIPES.items()
                          if document["executable"] in names]
            require(len(candidates) == 1, "no packaged image for catalog executable")
            role = candidates[0]
            require(type(document["revision"]) is int and 1 <= document["revision"] < 2147483647,
                    "container type revision cannot be incremented within the schema bound")
            document["revision"] += 1
        else:
            continue
        document["image"] = "graphx-" + role + "@" + manifest["images"][role]["inspection"]["digest"]
    lock["revision"] = "graphx-images-" + manifest["release_version"] + "-" + manifest["platform"].replace("/", "-")
    lock["binary_identity_status"] = "offline-image-bytes-verified; native-release-and-P8-guests-separate; execution-unavailable"
    documents["release-images.json"] = catalog_evidence(manifest)
    lock["files"].append({"path": "release-images.json", "sha256": ""})
    lock["files"].sort(key=lambda entry: entry["path"])
    # Publish only after all source pins and bindings have been checked.
    destination.mkdir()
    for entry in lock["files"]:
        path = destination / entry["path"]
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(encoded(documents[entry["path"]]))
        entry["sha256"] = sha256_file(path)
    (destination / "lock.json").write_bytes(encoded(lock))


def smoke_image(archive: Path, role: str, config_digest: str, manifest_digest: str) -> None:
    # Classic stores use the config digest; containerd stores use the OCI manifest.
    identity = config_digest
    existing = any(subprocess.run(["docker", "image", "inspect", candidate],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
                   for candidate in (config_digest, manifest_digest))
    try:
        subprocess.run(["docker", "image", "load", "--input", str(archive)], check=True, timeout=300)
        if subprocess.run(["docker", "image", "inspect", identity], stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode:
            identity = manifest_digest
        actual = json.loads(subprocess.check_output(["docker", "image", "inspect", identity], text=True))[0]
        require(actual["Id"] == identity, "loaded image identity differs from verified OCI bytes")
        commands = {"runtime": [["/usr/local/bin/graphx", "--version"]],
                    "telemetry": [["node", "--check", "/app/server.mjs"],
                                  ["node", "--input-type=module", "-e", "await import('ws'); await import('ajv');"]],
                    "sdr": [[name, "--help"] for name in RECIPES["sdr"][1]],
                    "vita": [[name, "--help"] for name in RECIPES["vita"][1]] + [["/usr/local/libexec/graphx-vita-recorder-test"]]}[role]
        for command in commands:
            container = subprocess.check_output(
                ["docker", "create", "--network", "none", "--read-only", "--cap-drop", "ALL",
                 "--security-opt", "no-new-privileges:true", "--pids-limit", "64", "--memory", "256m",
                 "--user", "65532:65532", "--entrypoint", command[0], identity, *command[1:]],
                text=True, timeout=30).strip()
            require(re.fullmatch(r"[a-f0-9]{64}", container), "Docker returned an invalid container identity")
            try:
                result = subprocess.run(["docker", "start", "--attach", container], check=False, timeout=30)
                # Radio/processor/detector reject missing config. Recorder fails
                # closed before configuration without NET_RAW; this smoke grants none.
                expected_exit = (1 if command[0] == "graphx-vita-recorder" else 78) if role == "vita" and command[0] in RECIPES["vita"][1] else 0
                require(result.returncode == expected_exit, "image smoke executable failed")
                state = subprocess.check_output(["docker", "inspect", "--format",
                                                 "{{.State.Status}} {{.State.ExitCode}}", container],
                                                text=True, timeout=30).strip()
                require(state == "exited " + str(expected_exit), "image smoke exit status mismatch")
            finally:
                # Remove only the exact container ID returned by this create,
                # including on interruption/timeout of the attached client.
                subprocess.run(["docker", "rm", "--force", container], check=True, timeout=30,
                               stdout=subprocess.DEVNULL)
    finally:
        if not existing:
            subprocess.run(["docker", "image", "rm", identity], check=False,
                           stdout=subprocess.DEVNULL)


def build(args):
    source = args.source.resolve()
    output = args.output.resolve()
    require(not output.exists(), "output must be absent")
    version = source_version(source)
    commit = validate_commit(subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip())
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=source))
    require(args.allow_dirty or not dirty, "release builds require a clean worktree")
    epoch = validate_epoch(int(subprocess.check_output(["git", "show", "-s", "--format=%ct", "HEAD"], cwd=source)))
    context = subprocess.check_output(["docker", "context", "show"], text=True).strip()
    require(sys.platform != "darwin" or context == "orbstack", "macOS image checks require the OrbStack context")
    subprocess.run(["docker", "info", "--format", "{{.OSType}}/{{.Architecture}}"], check=True)
    output.mkdir(parents=True)
    manifest = {"version": 1, "release_version": version, "commit": commit,
                "source_date_epoch": epoch, "dirty_candidate": dirty, "platform": args.platform,
                "execution_available": False, "qualification_hooks": args.with_vita, "images": {}}
    token = uuid.uuid4().hex
    for role, (dockerfile, _) in RECIPES.items():
        if role == "vita" and not args.with_vita:
            continue
        tag = "graphx-image-check-" + token + "-" + role
        try:
            for attempt in range(2):
                command = ["docker", "buildx", "build", "--platform", args.platform,
                           "--file", dockerfile, "--target", role, "--tag", tag, "--provenance=false",
                           "--build-arg", "GRAPHX_VERSION=" + version,
                           "--build-arg", "GRAPHX_REVISION=" + commit,
                           "--build-arg", "GRAPHX_RELEASE_IMAGE_TYPES=ON",
                           "--build-arg", "GRAPHX_BUILD_VITA_RADIO=" + ("ON" if args.with_vita else "OFF"),
                           "--build-arg", "GRAPHX_QUALIFICATION_HOOKS=" + ("ON" if args.with_vita else "OFF"),
                           "--build-arg", "SOURCE_DATE_EPOCH=" + str(epoch),
                           "--output", "type=image,oci-mediatypes=true,compression=uncompressed,force-compression=true"]
                if args.no_cache:
                    command.append("--no-cache")
                subprocess.run(command + ["."], cwd=source, check=True, timeout=1800)
                raw = output / (role + ".docker.tar")
                subprocess.run(["docker", "image", "save", "-o", str(raw), tag], check=True, timeout=300)
                inspect_image(raw, role, version, commit, args.platform)
                archive = output / (role + (".oci.tar" if attempt else ".first.oci.tar"))
                canonical_image(raw, archive, epoch)
                raw.unlink()
                inspection = inspect_image(archive, role, version, commit, args.platform)
                if attempt:
                    require(inspection == first and sha256_file(archive) == first_archive,
                            "independent image builds differ: " + role + "; retained both archives")
                first = inspection
                first_archive = sha256_file(archive)
            (output / (role + ".spdx.json")).write_bytes(encoded(image_sbom(role, inspection, version, epoch)))
            manifest["images"][role] = {"archive_sha256": sha256_file(archive), "inspection": inspection}
            smoke_image(archive, role, inspection["config_digest"], inspection["digest"])
            (output / (role + ".first.oci.tar")).unlink()
        finally:
            subprocess.run(["docker", "image", "rm", tag], check=False, stdout=subprocess.DEVNULL)
    manifest["repeat_builds"] = "no-cache" if args.no_cache else "cache-eligible"
    pin_catalog(source / "config/catalog", output / "catalog", manifest,
                source / "config/vita" if args.with_vita else None)
    manifest["catalog_sha256"] = sha256_file(output / "catalog/lock.json")
    (output / "images.json").write_bytes(encoded(manifest))
    verify(output)
    print("Verified shared image release and derived catalog: " + str(output))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    build_parser = sub.add_parser("build")
    build_parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[2])
    build_parser.add_argument("--output", type=Path, required=True)
    build_parser.add_argument("--platform", choices=("linux/arm64", "linux/amd64"), required=True)
    build_parser.add_argument("--allow-dirty", action="store_true")
    build_parser.add_argument("--no-cache", action="store_true")
    build_parser.add_argument("--with-vita", action="store_true",
                              help="include pinned private four-radio qualification applications")
    verify_parser = sub.add_parser("verify")
    verify_parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    if args.action == "build":
        build(args)
    else:
        verify(args.directory)
        print("Shared OCI image digests, layers and SPDX inventories verified")


if __name__ == "__main__":
    try:
        main()
    except (ReleaseError, OSError, ValueError, KeyError, TypeError, tarfile.TarError,
            subprocess.SubprocessError) as error:
        print("graphx-image-release: " + str(error), file=sys.stderr)
        sys.exit(2)
