#!/usr/bin/env python3
"""Portable behavioral tests for the shared SDR protocol, observer, and TLS control."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time


def import_path(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"cannot import {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def packet(protocol: int, source: str, destination: str, source_port: int,
           destination_port: int, payload: bytes) -> bytes:
    if protocol == 17:
        transport = struct.pack("!HHHH", source_port, destination_port, 8 + len(payload), 0)
    else:
        transport = struct.pack("!HHIIHHHH", source_port, destination_port, 0, 0,
                                5 << 12, 0, 0, 0)
    length = 20 + len(transport) + len(payload)
    ipv4 = struct.pack("!BBHHHBBH4s4s", 0x45, 0, length, 1, 0, 64, protocol, 0,
                       socket.inet_aton(source), socket.inet_aton(destination))
    return b"\x02" * 12 + b"\x08\x00" + ipv4 + transport + payload


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_sdr_example.py SOURCE_ROOT")
    root = Path(sys.argv[1]).resolve()
    common = root / "examples/sdr-node/common"
    sys.path.insert(0, str(common))
    protocol = import_path("sdr_protocol", common / "protocol.py")
    encoded = protocol.encode_samples(7, 915_000_000, 16)
    sequence, frequency, samples = protocol.decode_samples(encoded)
    assert sequence == 7 and frequency == 915_000_000 and len(samples) == 16
    assert encoded == protocol.encode_samples(7, 915_000_000, 16)
    for invalid in (b"", encoded[:-1], b"BAD!" + encoded[4:]):
        try:
            protocol.decode_samples(invalid)
            raise AssertionError("malformed sample payload was accepted")
        except ValueError:
            pass

    os.environ["GRAPHX_PACKET_RULES"] = (
        '[{"edge_id":"sdr-samples","direction":"sdr-to-processor",'
        '"node_id":"sdr-node","protocol":"UDP","source":"10.63.0.10",'
        '"destination":"10.63.0.20","destination_port":18400}]'
    )
    observer = import_path("sdr_packet_observer", root / "examples/qemu-node/tools/packet_observer.py")
    decoded = observer.decode_packet(packet(17, "10.63.0.10", "10.63.0.20", 40000,
                                             18400, encoded))
    assert decoded and decoded["edge_id"] == "sdr-samples" and decoded["node_id"] == "sdr-node"
    assert observer.decode_packet(packet(6, "10.63.0.10", "10.63.0.20", 40000,
                                         18400, b"wrong protocol")) is None

    with tempfile.TemporaryDirectory(prefix="graphx-sdr-tls-") as temporary:
        tls = Path(temporary)
        subprocess.run([common / "generate_tls.sh", tls], check=True)
        with socket.socket() as candidate:
            candidate.bind(("127.0.0.1", 0))
            port = candidate.getsockname()[1]
        os.environ.update({"SDR_CONTROL_PORT": str(port), "SDR_CONTROL_TARGET": "127.0.0.1",
                           "SDR_TLS_CERT": str(tls / "sdr-node.pem"),
                           "SDR_TLS_KEY": str(tls / "sdr-node.key"),
                           "SDR_TLS_CLIENT_CA": str(tls / "ca.pem")})
        simulator = import_path("sdr_simulator_test", common / "sdr_simulator.py")
        server = threading.Thread(target=simulator.control_server, daemon=True)
        server.start()
        # Force the server to finish reading its own identity before changing
        # this process environment to the processor's client identity.
        for _ in range(30):
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                    break
            except OSError:
                time.sleep(0.05)
        else:
            raise AssertionError("SDR control listener did not become ready")
        os.environ.update({"SDR_TLS_CERT": str(tls / "processor.pem"),
                           "SDR_TLS_KEY": str(tls / "processor.key"),
                           "SDR_TLS_CA": str(tls / "ca.pem"),
                           "SDR_TLS_SERVER_NAME": "sdr-node"})
        processor = import_path("sdr_processor_test", common / "processor.py")
        unauthorized = ssl.create_default_context(ssl.Purpose.SERVER_AUTH,
                                                   cafile=str(tls / "ca.pem"))
        unauthorized.minimum_version = ssl.TLSVersion.TLSv1_3
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1) as raw:
                with unauthorized.wrap_socket(raw, server_hostname="sdr-node") as secure:
                    secure.sendall(b'{"action":"status"}\n')
                    if secure.recv(4096):
                        raise AssertionError("control accepted a client without a certificate")
        except (ConnectionError, ssl.SSLError):
            pass
        wrong_name = ssl.create_default_context(ssl.Purpose.SERVER_AUTH,
                                                 cafile=str(tls / "ca.pem"))
        wrong_name.minimum_version = ssl.TLSVersion.TLSv1_3
        wrong_name.load_cert_chain(tls / "processor.pem", tls / "processor.key")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1) as raw:
                with wrong_name.wrap_socket(raw, server_hostname="not-the-sdr"):
                    raise AssertionError("control accepted the wrong server identity")
        except ssl.SSLError:
            pass
        for _ in range(30):
            try:
                assert processor.control("status")["accepted"]
                break
            except OSError:
                time.sleep(0.05)
        else:
            raise AssertionError("mutual-TLS SDR control server did not become ready")
        assert processor.control("tune", 433_920_000)["frequency_hz"] == 433_920_000
        try:
            processor.control("tune", 1)
            raise AssertionError("out-of-range tune was accepted")
        except RuntimeError:
            pass
        assert processor.control("stop")["running"] is False
        assert processor.control("start")["running"] is True
        simulator.stop.set()
        server.join(timeout=2)

    for script in (common / "generate_tls.sh",
                   root / "examples/sdr-node/simulated/scripts/demo.sh",
                   root / "examples/sdr-node/external/scripts/demo.sh"):
        subprocess.run(["bash", "-n", script], check=True)
    print("SDR example portable behavioral checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
