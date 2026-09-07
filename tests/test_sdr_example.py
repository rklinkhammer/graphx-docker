#!/usr/bin/env python3
"""Portable behavioral tests for the shared SDR protocol, observer, and TLS control."""

from __future__ import annotations

import importlib.util
import json
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


def expect_value_error(operation, message: str) -> None:
    try:
        operation()
        raise AssertionError(message)
    except ValueError:
        pass


def tls_context(ca: Path, certificate: Path, key: Path) -> ssl.SSLContext:
    context = ssl.create_default_context(ssl.Purpose.SERVER_AUTH, cafile=str(ca))
    context.minimum_version = ssl.TLSVersion.TLSv1_3
    context.load_cert_chain(certificate, key)
    return context


def tls_request(context: ssl.SSLContext, port: int, request: bytes,
                server_name: str = "sdr-node") -> bytes:
    with socket.create_connection(("127.0.0.1", port), timeout=1) as raw:
        with context.wrap_socket(raw, server_hostname=server_name) as secure:
            secure.settimeout(1)
            secure.sendall(request)
            return secure.recv(4096)


def generate_expired_client_pki(directory: Path) -> None:
    """Generate a test-only CA/server and a client whose validity is zero days."""
    directory.mkdir()
    quiet = {"stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL, "check": True}
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-sha256",
                    "-nodes", "-days", "1", "-subj", "/CN=GraphX-expiry-test-CA",
                    "-keyout", directory / "ca.key", "-out", directory / "ca.pem"], **quiet)
    for identity in ("sdr-node", "expired-processor"):
        subprocess.run(["openssl", "req", "-newkey", "rsa:2048", "-sha256", "-nodes",
                        "-subj", f"/CN={identity}", "-keyout", directory / f"{identity}.key",
                        "-out", directory / f"{identity}.csr"], **quiet)
    (directory / "server.ext").write_text(
        "subjectAltName=DNS:sdr-node\nextendedKeyUsage=serverAuth\n", encoding="utf-8")
    (directory / "client.ext").write_text("extendedKeyUsage=clientAuth\n", encoding="utf-8")
    subprocess.run(["openssl", "x509", "-req", "-sha256", "-days", "1",
                    "-in", directory / "sdr-node.csr", "-CA", directory / "ca.pem",
                    "-CAkey", directory / "ca.key", "-CAcreateserial", "-extfile",
                    directory / "server.ext", "-out", directory / "sdr-node.pem"], **quiet)
    subprocess.run(["openssl", "x509", "-req", "-sha256", "-days", "0",
                    "-in", directory / "expired-processor.csr", "-CA", directory / "ca.pem",
                    "-CAkey", directory / "ca.key", "-CAcreateserial", "-extfile",
                    directory / "client.ext", "-out", directory / "expired-processor.pem"],
                   **quiet)


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
    for count in (1, protocol.MAX_SAMPLES):
        boundary = protocol.encode_samples(0xFFFFFFFF, 6_000_000_000, count)
        assert protocol.decode_samples(boundary)[:2] == (0xFFFFFFFF, 6_000_000_000)
        assert len(protocol.decode_samples(boundary)[2]) == count
    for operation in (
            lambda: protocol.encode_samples(-1, 1_000_000, 1),
            lambda: protocol.encode_samples(0, 999_999, 1),
            lambda: protocol.encode_samples(0, 1_000_000, 0),
            lambda: protocol.encode_samples(0, 1_000_000, protocol.MAX_SAMPLES + 1)):
        expect_value_error(operation, "invalid encoder boundary was accepted")
    bad_count = bytearray(encoded)
    struct.pack_into("!H", bad_count, protocol.HEADER.size - 2, 17)
    bad_frequency = bytearray(encoded)
    struct.pack_into("!Q", bad_frequency, 8, 999_999)
    for invalid in (b"", encoded[:-1], encoded + b"x", b"BAD!" + encoded[4:],
                    bytes(bad_count), bytes(bad_frequency)):
        expect_value_error(lambda value=invalid: protocol.decode_samples(value),
                           "malformed sample payload was accepted")
    # UDP delivery is intentionally stateless: loss, duplicates, reordering,
    # and a restarted sequence are all independently parseable and observable.
    assert [protocol.decode_samples(protocol.encode_samples(value, 100_000_000, 1))[0]
            for value in (10, 10, 12, 11, 0)] == [10, 10, 12, 11, 0]

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
    processor_module = import_path("sdr_processor_endpoint_test", common / "processor.py")
    assert processor_module.expected_sample_source("10.63.0.10") == "10.63.0.10"
    expect_value_error(lambda: processor_module.expected_sample_source("not-an-address"),
                       "invalid sample source was accepted")
    expect_value_error(lambda: processor_module.expected_sample_source("::1"),
                       "IPv6 source was accepted for the IPv4 profile")
    assert processor_module.decode_sample_datagram(encoded, ("10.63.0.10", 49152),
                                                   "10.63.0.10")[0] == 7
    expect_value_error(
        lambda: processor_module.decode_sample_datagram(encoded, ("10.63.0.99", 49152),
                                                        "10.63.0.10"),
        "mismatched UDP source was accepted")

    with tempfile.TemporaryDirectory(prefix="graphx-sdr-tls-") as temporary:
        tls = Path(temporary)
        subprocess.run([common / "generate_tls.sh", tls], check=True)
        listener = socket.socket()
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", 0))
        listener.listen(8)
        port = listener.getsockname()[1]
        os.environ.update({"SDR_CONTROL_PORT": str(port), "SDR_CONTROL_TARGET": "127.0.0.1",
                           "SDR_TLS_CERT": str(tls / "sdr-node.pem"),
                           "SDR_TLS_KEY": str(tls / "sdr-node.key"),
                           "SDR_TLS_CLIENT_CA": str(tls / "ca.pem")})
        simulator = import_path("sdr_simulator_test", common / "sdr_simulator.py")
        for invalid_command in (
                None, [], {}, {"action": "unknown"}, {"action": "tune"},
                {"action": "tune", "frequency_hz": "433920000"},
                {"action": "status", "frequency_hz": 433_920_000},
                {"action": "start", "frequency_hz": 433_920_000},
                {"action": "stop", "extra": True}):
            assert simulator.apply_command(invalid_command)["accepted"] is False
        for malformed_line in (b"unterminated", b"a" * 4097 + b"\n",
                               b"{}\n{}\n", b"{}\ntrailing"):
            left, right = socket.socketpair()
            with left, right:
                left.sendall(malformed_line)
                left.shutdown(socket.SHUT_WR)
                expect_value_error(lambda endpoint=right: protocol.recv_line(endpoint, 4096),
                                   "malformed control framing was accepted")
        server = threading.Thread(target=simulator.control_server, args=(listener,), daemon=True)
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
        valid_context = tls_context(tls / "ca.pem", tls / "processor.pem",
                                    tls / "processor.key")
        wrong_name = valid_context
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1) as raw:
                with wrong_name.wrap_socket(raw, server_hostname="not-the-sdr"):
                    raise AssertionError("control accepted the wrong server identity")
        except ssl.SSLError:
            pass
        untrusted_dir = tls / "untrusted"
        subprocess.run([common / "generate_tls.sh", untrusted_dir], check=True)
        untrusted = tls_context(tls / "ca.pem", untrusted_dir / "processor.pem",
                                untrusted_dir / "processor.key")
        try:
            tls_request(untrusted, port, b'{"action":"status"}\n')
            raise AssertionError("control accepted an untrusted client certificate")
        except (ConnectionError, OSError, ssl.SSLError):
            pass
        for _ in range(30):
            try:
                assert processor.control("status")["accepted"]
                break
            except OSError:
                time.sleep(0.05)
        else:
            raise AssertionError("mutual-TLS SDR control server did not become ready")
        for malformed in (b"not-json\n", b"\xff\n", b"{}\n{}\n", b"x" * 4097 + b"\n"):
            try:
                assert tls_request(valid_context, port, malformed) == b""
            except (ConnectionError, OSError, ssl.SSLError):
                pass
        rejected = json.loads(tls_request(valid_context, port, b'{"action":"explode"}\n'))
        assert rejected["accepted"] is False
        rejected = json.loads(tls_request(
            valid_context, port, b'{"action":"status","frequency_hz":433920000}\n'))
        assert rejected["accepted"] is False
        # Identical requests on fresh TLS connections are independently valid;
        # no connection state or replay token is silently reused.
        for _ in range(2):
            assert json.loads(tls_request(valid_context, port, b'{"action":"status"}\n'))[
                "accepted"]
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

        expiry = tls / "expiry"
        generate_expired_client_pki(expiry)
        time.sleep(1.1)
        expiry_listener = socket.socket()
        expiry_listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        expiry_listener.bind(("127.0.0.1", 0))
        expiry_listener.listen(8)
        expiry_port = expiry_listener.getsockname()[1]
        os.environ.update({"SDR_CONTROL_PORT": str(expiry_port),
                           "SDR_TLS_CERT": str(expiry / "sdr-node.pem"),
                           "SDR_TLS_KEY": str(expiry / "sdr-node.key"),
                           "SDR_TLS_CLIENT_CA": str(expiry / "ca.pem")})
        simulator.stop.clear()
        expiry_server = threading.Thread(target=simulator.control_server, args=(expiry_listener,),
                                         daemon=True)
        expiry_server.start()
        for _ in range(30):
            try:
                with socket.create_connection(("127.0.0.1", expiry_port), timeout=0.1):
                    break
            except OSError:
                time.sleep(0.05)
        expired = tls_context(expiry / "ca.pem", expiry / "expired-processor.pem",
                              expiry / "expired-processor.key")
        try:
            tls_request(expired, expiry_port, b'{"action":"status"}\n')
            raise AssertionError("control accepted an expired client certificate")
        except (ConnectionError, OSError, ssl.SSLError):
            pass
        simulator.stop.set()
        expiry_server.join(timeout=2)

    lifecycle = common / "lifecycle.sh"
    for script in (common / "generate_tls.sh", lifecycle,
                   root / "examples/sdr-node/simulated/scripts/demo.sh",
                   root / "examples/sdr-node/external/scripts/demo.sh"):
        subprocess.run(["bash", "-n", script], check=True)
    with socket.socket() as occupied:
        occupied.bind(("127.0.0.1", 0))
        occupied.listen(1)
        occupied_port = str(occupied.getsockname()[1])
        environment = {**os.environ, "GRAPHX_SDR_PORT_ATTEMPTS": "1"}
        result = subprocess.run(
            ["bash", "-c", 'source "$1"; graphx_sdr_preflight_port "$2"',
             "graphx-sdr-port-test", str(lifecycle), occupied_port],
            capture_output=True, text=True, env=environment, check=False)
        assert result.returncode != 0
        assert f"port {occupied_port} is unavailable" in result.stderr
        assert "NameError" not in result.stderr
    result = subprocess.run(
        ["bash", "-c", 'source "$1"; graphx_sdr_preflight_port invalid',
         "graphx-sdr-port-test", str(lifecycle)], capture_output=True, text=True,
        check=False)
    assert result.returncode != 0 and "must be an integer" in result.stderr

    simulated_script = (root / "examples/sdr-node/simulated/scripts/demo.sh").read_text()
    external_script = (root / "examples/sdr-node/external/scripts/demo.sh").read_text()
    simulated_compose = (root / "examples/sdr-node/simulated/compose.yaml").read_text()
    external_compose = (root / "examples/sdr-node/external/compose.yaml").read_text()
    assert "rolling back owned portable resources" in simulated_script
    assert "requested_gui_port" in simulated_script and "graphx_sdr_preflight_port" in simulated_script
    assert 'command: ["tcpdump", "-Z", "root"' in simulated_compose
    assert 'user: "0:0"' in simulated_compose and "security_opt: []" in simulated_compose
    assert "cap_add: [NET_RAW, NET_ADMIN, SETUID, SETGID]" in simulated_compose
    assert "SDR_SAMPLE_SOURCE: 172.30.13.10" in simulated_compose
    assert "SDR_SAMPLE_SOURCE: 10.63.0.10" in external_compose
    for required in ("GRAPHX_SDR_OWNER", "com.graphx.sdr.owner", "graphx_sdr_owner",
                     "Refusing to remove native SDR resources", 'chown "$3:$4"',
                     'chown "$5:$6"', "current_start_owns_native=true"):
        assert required in external_script, f"external lifecycle is missing {required}"
    print("SDR example portable behavioral checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
