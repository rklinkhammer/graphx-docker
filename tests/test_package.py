#!/usr/bin/env python3
"""Install, consume, and inspect the native release package."""

from __future__ import annotations

import copy
import io
import os
import stat
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path, PurePosixPath

SOURCE = Path(sys.argv[1]).resolve()
BUILD = Path(sys.argv[2]).resolve()
VERSION = (SOURCE / "VERSION").read_text(encoding="utf-8").strip()
sys.path.insert(0, str(SOURCE / "scripts/release"))

from release_common import (ReleaseError, archive_file_contract, current_platform,  # noqa: E402
                            verify_release_archive)

CACHE = (BUILD / "CMakeCache.txt").read_text(encoding="utf-8")
IS_RELEASE = "CMAKE_BUILD_TYPE:STRING=Release" in CACHE


def run(command: list[str], cwd: Path | None = None) -> None:
    subprocess.run(command, cwd=cwd, check=True,
                   env={**os.environ, "GRAPHX_OVERRIDES": "", "COPYFILE_DISABLE": "1"})


def rejected(action) -> None:
    try:
        action()
    except ReleaseError:
        return
    raise AssertionError("invalid packaged archive was accepted")


def rewrite_archive(source: Path, destination: Path, *, omitted: str | None = None,
                    mode_name: str | None = None, mode: int = 0o644) -> None:
    with tarfile.open(source, "r:gz") as input_archive, tarfile.open(
            destination, "w:gz") as output_archive:
        for original in input_archive:
            if original.name == omitted:
                continue
            member = copy.copy(original)
            if member.name == mode_name:
                member.mode = mode
            payload = None
            if member.isfile():
                stream = input_archive.extractfile(original)
                assert stream is not None
                payload = io.BytesIO(stream.read())
            output_archive.addfile(member, payload)


with tempfile.TemporaryDirectory(prefix="graphx-package-test-") as temporary:
    root = Path(temporary)
    prefix = root / "prefix"
    run(["cmake", "--install", str(BUILD), "--prefix", str(prefix)])
    executable = prefix / "bin" / "graphx"
    required = [
        executable, prefix / "lib" / "libgraphx.a",
        prefix / "include" / "graphx" / "version.hpp",
        prefix / "lib" / "cmake" / "GraphX" / "GraphXConfig.cmake",
        prefix / "libexec" / "graphx" / "graphx-extcap",
        prefix / "share" / "graphx" / "wireshark" / "graphx.lua",
        prefix / "share" / "graphx" / "schema" / "graphx.schema.json",
        prefix / "share" / "doc" / "graphx" / "LICENSE",
    ]
    assert all(path.is_file() for path in required), [str(path) for path in required if not path.is_file()]
    assert subprocess.check_output([executable, "--version"], text=True) == f"graphx {VERSION}\n"

    contract = archive_file_contract(VERSION, current_platform())
    if IS_RELEASE:
        package_root = f"graphx-{VERSION}-{current_platform()}"
        expected = {str(PurePosixPath(name).relative_to(package_root)): mode
                    for name, mode in contract.items()}
        installed = {
            path.relative_to(prefix).as_posix(): stat.S_IMODE(path.stat().st_mode)
            for path in prefix.rglob("*") if path.is_file() and not path.is_symlink()
        }
        assert set(installed) == set(expected), {
            "missing": sorted(set(expected) - set(installed)),
            "unexpected": sorted(set(installed) - set(expected)),
        }
        assert installed == expected, {
            name: {"actual": installed[name], "expected": expected[name]}
            for name in expected if installed[name] != expected[name]
        }

    consumer = root / "consumer"
    consumer.mkdir()
    (consumer / "CMakeLists.txt").write_text("""cmake_minimum_required(VERSION 3.25)
project(graphx_consumer LANGUAGES CXX)
find_package(GraphX CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE GraphX::graphx)
""", encoding="utf-8")
    (consumer / "main.cpp").write_text(f"""#include <graphx/envelope.hpp>
#include <graphx/version.hpp>
int main() {{
  return graphx::version == \"{VERSION}\" &&
    graphx::is_canonical_identity(\"0123456789abcdef0123456789abcdef\") ? 0 : 1;
}}
""", encoding="utf-8")
    consumer_build = root / "consumer-build"
    run(["cmake", "-S", str(consumer), "-B", str(consumer_build), "-G", "Ninja",
         f"-DCMAKE_PREFIX_PATH={prefix}"])
    run(["cmake", "--build", str(consumer_build)])
    run([str(consumer_build / "consumer")])

    package_dir = root / "packages"
    run(["cpack", "--config", str(BUILD / "CPackConfig.cmake"), "-G", "TGZ",
         "-B", str(package_dir)])
    archives = list(package_dir.glob("graphx-*.tar.gz"))
    assert len(archives) == 1, archives
    if IS_RELEASE:
        verify_release_archive(archives[0], VERSION, current_platform())
        for index, required_name in enumerate(sorted(contract)):
            omitted_archive = package_dir / f"omitted-{index}.tar.gz"
            rewrite_archive(archives[0], omitted_archive, omitted=required_name)
            rejected(lambda path=omitted_archive: verify_release_archive(
                path, VERSION, current_platform()))
        for index, executable_name in enumerate(sorted(
                name for name, mode in contract.items() if mode == 0o755)):
            non_executable_archive = package_dir / f"non-executable-{index}.tar.gz"
            rewrite_archive(archives[0], non_executable_archive,
                            mode_name=executable_name, mode=0o644)
            rejected(lambda path=non_executable_archive: verify_release_archive(
                path, VERSION, current_platform()))
    with tarfile.open(archives[0], "r:gz") as archive:
        names = [member.name for member in archive.getmembers()]
        assert all(not PurePosixPath(name).is_absolute() and ".." not in PurePosixPath(name).parts
                   for name in names)
        assert any(name.endswith("/bin/graphx") for name in names)
        assert any(name.endswith("/include/graphx/version.hpp") for name in names)
        assert any(name.endswith("/share/graphx/wireshark/graphx.lua") for name in names)

print("GraphX install, consumer, and package validation passed")
