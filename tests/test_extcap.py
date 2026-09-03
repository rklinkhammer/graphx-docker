#!/usr/bin/env python3
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def run(command: list[str], expected: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, text=True, capture_output=True, timeout=10)
    if result.returncode != expected:
        raise RuntimeError(f"unexpected exit {result.returncode}: {' '.join(command)}\n{result.stderr}")
    return result


def main() -> int:
    if len(sys.argv) != 3:
        raise RuntimeError("usage: test_extcap.py EXTCAP FIXTURE_WRITER")
    extcap, fixture_writer = map(str, map(Path, sys.argv[1:]))
    with tempfile.TemporaryDirectory(prefix="graphx-extcap-") as directory:
        root = Path(directory)
        capture = root / "fixture.pcapng"
        copied = root / "copied.pcapng"
        run([fixture_writer, str(capture)])
        assert "value=graphx" in run([extcap, "--extcap-interfaces"]).stdout
        assert "number=147" in run(
            [extcap, "--extcap-dlts", "--extcap-interface", "graphx"]
        ).stdout
        assert "version=1.0.0" in run([extcap, "--extcap-version", "4.4"]).stdout
        assert "Capture filters are not supported" in run(
            [extcap, "--extcap-interface", "graphx", "--extcap-capture-filter", "tcp"]
        ).stdout
        run([extcap, "--capture", "--extcap-interface", "graphx", "--capture-file",
             str(capture), "--follow", "false", "--fifo", str(copied)])
        assert capture.read_bytes() == copied.read_bytes()
        complete = capture.read_bytes()
        first_length = int.from_bytes(complete[4:8], "little")
        second_length = int.from_bytes(complete[first_length + 4:first_length + 8], "little")
        initial_length = first_length + second_length
        live = root / "live.pcapng"
        followed = root / "followed.pcapng"
        live.write_bytes(complete[:initial_length])
        process = subprocess.Popen([
            extcap, "--capture", "--extcap-interface", "graphx", "--capture-file",
            str(live), "--follow", "true", "--fifo", str(followed),
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            for _ in range(100):
                if followed.exists() and followed.stat().st_size == initial_length:
                    break
                time.sleep(0.01)
            with live.open("ab") as output:
                output.write(complete[initial_length:])
                output.flush()
            for _ in range(100):
                if followed.exists() and followed.stat().st_size == len(complete):
                    break
                time.sleep(0.01)
            assert followed.read_bytes() == complete
        finally:
            process.terminate()
            process.wait(timeout=5)
        mismatch = run([extcap, "--capture", "--extcap-interface", "graphx-ethernet",
                        "--capture-file", str(capture), "--follow", "false", "--fifo",
                        str(copied)], 2)
        assert "does not match" in mismatch.stderr
        filtered = run([extcap, "--capture", "--extcap-interface", "graphx",
                        "--capture-file", str(capture), "--follow", "false", "--fifo",
                        str(copied), "--extcap-capture-filter", "tcp"], 2)
        assert "not supported" in filtered.stderr
        broken = root / "broken.pcapng"
        broken.write_bytes(capture.read_bytes()[:-1])
        assert "truncated" in run([extcap, "--capture", "--extcap-interface", "graphx",
                                   "--capture-file", str(broken), "--follow", "false",
                                   "--fifo", str(copied)], 2).stderr
        oversized = root / "oversized.pcapng"
        invalid = bytearray(capture.read_bytes())
        invalid[4:8] = (17 * 1024 * 1024 + 4).to_bytes(4, "little")
        oversized.write_bytes(invalid)
        assert "invalid PCAPNG block length" in run(
            [extcap, "--capture", "--extcap-interface", "graphx", "--capture-file",
             str(oversized), "--follow", "false", "--fifo", str(copied)], 2
        ).stderr
        bad_trailer = root / "bad-trailer.pcapng"
        invalid = bytearray(capture.read_bytes())
        invalid[first_length - 4:first_length] = (0).to_bytes(4, "little")
        bad_trailer.write_bytes(invalid)
        assert "trailer does not match" in run(
            [extcap, "--capture", "--extcap-interface", "graphx", "--capture-file",
             str(bad_trailer), "--follow", "false", "--fifo", str(copied)], 2
        ).stderr
        for major, minor in ((0, 9), (1, 1), (2, 0)):
            unsupported_version = root / f"unsupported-{major}-{minor}.pcapng"
            invalid = bytearray(capture.read_bytes())
            invalid[12:16] = major.to_bytes(2, "little") + minor.to_bytes(2, "little")
            unsupported_version.write_bytes(invalid)
            assert "unsupported PCAPNG section version" in run(
                [extcap, "--capture", "--extcap-interface", "graphx", "--capture-file",
                 str(unsupported_version), "--follow", "false", "--fifo", str(copied)], 2
            ).stderr
        invalid_section_length = root / "invalid-section-length.pcapng"
        invalid = bytearray(capture.read_bytes())
        invalid[16:24] = (0).to_bytes(8, "little")
        invalid_section_length.write_bytes(invalid)
        assert "unsupported PCAPNG section length" in run(
            [extcap, "--capture", "--extcap-interface", "graphx", "--capture-file",
             str(invalid_section_length), "--follow", "false", "--fifo", str(copied)], 2
        ).stderr
        short_section = root / "short-section.pcapng"
        invalid = bytearray(complete[:24])
        invalid[4:8] = (24).to_bytes(4, "little")
        invalid[20:24] = (24).to_bytes(4, "little")
        short_section.write_bytes(invalid)
        assert "section header" in run(
            [extcap, "--capture", "--extcap-interface", "graphx", "--capture-file",
             str(short_section), "--follow", "false", "--fifo", str(copied)], 2
        ).stderr
        packet_offset = initial_length
        packet_length = int.from_bytes(complete[packet_offset + 4:packet_offset + 8], "little")
        packet = complete[packet_offset:packet_offset + packet_length]
        packet_before_interface = root / "packet-before-interface.pcapng"
        packet_before_interface.write_bytes(complete[:first_length] + packet)
        assert "interface description must follow" in run(
            [extcap, "--capture", "--extcap-interface", "graphx", "--capture-file",
             str(packet_before_interface), "--follow", "false", "--fifo", str(copied)], 2
        ).stderr
        second_section = root / "second-section.pcapng"
        second_section.write_bytes(complete + complete[:first_length])
        assert "multiple PCAPNG sections" in run(
            [extcap, "--capture", "--extcap-interface", "graphx", "--capture-file",
             str(second_section), "--follow", "false", "--fifo", str(copied)], 2
        ).stderr
        second_interface = root / "second-interface.pcapng"
        second_interface.write_bytes(complete + complete[first_length:initial_length])
        assert "multiple PCAPNG interfaces" in run(
            [extcap, "--capture", "--extcap-interface", "graphx", "--capture-file",
             str(second_interface), "--follow", "false", "--fifo", str(copied)], 2
        ).stderr
        wrong_interface = root / "wrong-packet-interface.pcapng"
        invalid = bytearray(complete)
        invalid[packet_offset + 8:packet_offset + 12] = (1).to_bytes(4, "little")
        wrong_interface.write_bytes(invalid)
        assert "unsupported interface" in run(
            [extcap, "--capture", "--extcap-interface", "graphx", "--capture-file",
             str(wrong_interface), "--follow", "false", "--fifo", str(copied)], 2
        ).stderr
        unknown_block = root / "unknown-block.pcapng"
        unsupported = bytearray(12)
        unsupported[0:4] = (3).to_bytes(4, "little")
        unsupported[4:8] = (12).to_bytes(4, "little")
        unsupported[8:12] = (12).to_bytes(4, "little")
        unknown_block.write_bytes(complete + unsupported)
        assert "unsupported PCAPNG block" in run(
            [extcap, "--capture", "--extcap-interface", "graphx", "--capture-file",
             str(unknown_block), "--follow", "false", "--fifo", str(copied)], 2
        ).stderr
        link = root / "linked.pcapng"
        link.symlink_to(capture)
        assert "non-symlink" in run([extcap, "--capture", "--extcap-interface", "graphx",
                                     "--capture-file", str(link), "--follow", "false",
                                     "--fifo", str(copied)], 2).stderr
        fifo_source = root / "capture-fifo.pcapng"
        os.mkfifo(fifo_source, 0o600)
        started = time.monotonic()
        assert "regular non-symlink" in run(
            [extcap, "--capture", "--extcap-interface", "graphx", "--capture-file",
             str(fifo_source), "--follow", "false", "--fifo", str(copied)], 2
        ).stderr
        assert time.monotonic() - started < 1, "FIFO source validation blocked"
    print("GraphX extcap validation passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
