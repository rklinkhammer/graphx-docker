#!/usr/bin/env python3
"""Bounded SDR demo wire format and GraphX telemetry helpers."""

from __future__ import annotations

import hashlib
import hmac
import json
import os
import secrets
import socket
import struct
import time
from functools import lru_cache

MAGIC = b"SDR1"
HEADER = struct.Struct("!4sIQH")
SAMPLE = struct.Struct("!hh")
MAX_SAMPLES = 256


@lru_cache(maxsize=1)
def telemetry_endpoint() -> tuple[str, int]:
    """Read the shared telemetry destination from normalized GraphX configuration."""
    path = os.environ.get("GRAPHX_NORMALIZED_CONFIG", "")
    if not path:
        return "telemetry", 9000
    with open(path, encoding="utf-8") as stream:
        telemetry = json.load(stream)["observability"]["telemetry"]
    return telemetry["host"], telemetry["port"]


def recv_line(connection: socket.socket, maximum: int = 4096) -> bytes:
    """Receive exactly one bounded line from a one-request connection."""
    value = bytearray()
    while b"\n" not in value:
        chunk = connection.recv(min(1024, maximum + 1 - len(value)))
        if not chunk:
            raise ValueError("connection closed before the line terminator")
        value.extend(chunk)
        if len(value) > maximum:
            raise ValueError("line exceeds the configured bound")
    if value[-1:] != b"\n" or value.count(b"\n") != 1:
        raise ValueError("connection contains trailing or multiple records")
    return bytes(value)


def encode_samples(sequence: int, frequency_hz: int, count: int = 16) -> bytes:
    if not 0 <= sequence <= 0xFFFFFFFF or not 1_000_000 <= frequency_hz <= 6_000_000_000:
        raise ValueError("invalid SDR sequence or frequency")
    if not 1 <= count <= MAX_SAMPLES:
        raise ValueError("sample count is out of range")
    pairs = b"".join(SAMPLE.pack((sequence + index) % 32768, -((sequence + index) % 32768))
                     for index in range(count))
    return HEADER.pack(MAGIC, sequence, frequency_hz, count) + pairs


def decode_samples(payload: bytes) -> tuple[int, int, list[tuple[int, int]]]:
    if len(payload) < HEADER.size:
        raise ValueError("truncated SDR header")
    magic, sequence, frequency_hz, count = HEADER.unpack_from(payload)
    if magic != MAGIC or not 1_000_000 <= frequency_hz <= 6_000_000_000 or \
            not 1 <= count <= MAX_SAMPLES:
        raise ValueError("invalid SDR header")
    if len(payload) != HEADER.size + count * SAMPLE.size:
        raise ValueError("SDR payload length does not match sample count")
    samples = [SAMPLE.unpack_from(payload, HEADER.size + index * SAMPLE.size)
               for index in range(count)]
    return sequence, frequency_hz, samples


def signed(payload: dict[str, object], secret: str) -> bytes:
    payload = {**payload, **execution_context()}
    if not secret:
        return json.dumps(payload, separators=(",", ":")).encode()
    timestamp = int(time.time() * 1000)
    nonce = secrets.token_hex(16)
    compact = json.dumps(payload, separators=(",", ":"))
    signature = hmac.new(secret.encode(), f"{timestamp}.{nonce}.{compact}".encode(),
                         hashlib.sha256).hexdigest()
    return json.dumps({"payload": payload, "auth": {"timestamp": timestamp, "nonce": nonce,
                                                      "signature": signature}},
                      separators=(",", ":")).encode()


def verified(data: bytes, secret: str, replay: dict | None = None) -> dict[str, object] | None:
    """Verify one bounded telemetry/control datagram."""
    try:
        value = json.loads(data)
        if not secret:
            return value if isinstance(value, dict) and "auth" not in value else None
        payload, auth = value["payload"], value["auth"]
        timestamp = auth["timestamp"]
        nonce = auth["nonce"]
        supplied = auth["signature"]
        if not isinstance(timestamp, int) or abs(int(time.time() * 1000) - timestamp) > 30_000:
            return None
        if not isinstance(nonce, str) or len(nonce) != 32 or not isinstance(supplied, str):
            return None
        context = execution_context()
        if context and any(payload.get(key) != value for key, value in context.items() if key != "nodeId"):
            return None
        if context and payload.get("targetNode") != context["nodeId"]:
            return None
        if context:
            import re
            expires = payload.get("expiresAt")
            now = int(time.time() * 1000)
            if payload.get("kind") != "control" or type(expires) is not int or not now <= expires <= now + 30_000:
                return None
            if not isinstance(payload.get("commandId"), str) or not re.fullmatch(
                    r"[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}",
                    payload["commandId"]):
                return None
        compact = json.dumps(payload, separators=(",", ":"))
        expected = hmac.new(secret.encode(), f"{timestamp}.{nonce}.{compact}".encode(),
                            hashlib.sha256).hexdigest()
        if not hmac.compare_digest(expected, supplied):
            return None
        if replay is not None:
            now = int(time.time() * 1000)
            for key, expires in list(replay.items()):
                if expires < now:
                    del replay[key]
            if nonce in replay or len(replay) >= 4096:
                return None
            replay[nonce] = timestamp + 30_000
        return payload
    except (KeyError, TypeError, ValueError, json.JSONDecodeError):
        return None


def publish_heartbeat(node_id: str, sequence: int) -> None:
    host, port = telemetry_endpoint()
    secret = telemetry_secret()
    event = {"kind": "trace", "event": "heartbeat", "nodeId": node_id,
             "timestamp": int(time.time() * 1000), "sequence": sequence}
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as output:
            output.sendto(signed(event, secret), (host, port))
    except OSError:
        pass


@lru_cache(maxsize=1)
def normalized_config() -> dict | None:
    path = os.environ.get("GRAPHX_NORMALIZED_CONFIG", "")
    if not path:
        return None
    with open(path, encoding="utf-8") as stream:
        raw = stream.read(1024 * 1024 + 1)
        if len(raw) > 1024 * 1024:
            raise ValueError("normalized configuration exceeds 1 MiB")
        value = json.loads(raw)
    if value.get("contract_version") != 1:
        raise ValueError("unsupported normalized configuration contract")
    return value


@lru_cache(maxsize=1)
def execution_context() -> dict[str, str]:
    import re
    config = normalized_config()
    instance = config.get("deployment", {}).get("instance_id") if config else None
    if not instance:
        return {}
    if any(not isinstance(value, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_-]{0,63}", value)
           for value in [config["graph"]["id"], instance]):
        raise ValueError("invalid normalized graph/instance identity")
    node = os.environ.get("GRAPHX_NODE_ID", "")
    execution = os.environ.get("GRAPHX_EXECUTION_ID", "")
    if sum(item["id"] == node for item in config["graph"]["nodes"]) != 1:
        raise ValueError("explicit known GRAPHX_NODE_ID is required")
    if not re.fullmatch(r"[0-9a-f]{32}", execution) or execution == "0" * 32:
        raise ValueError("a registered GRAPHX_EXECUTION_ID is required")
    if len(telemetry_secret()) < 32:
        raise ValueError("instance telemetry requires a node credential")
    return {"graphId": config["graph"]["id"], "instanceId": instance,
            "nodeId": node, "executionId": execution}


def telemetry_secret() -> str:
    inline = os.environ.get("GRAPHX_TELEMETRY_SHARED_SECRET", "")
    path = os.environ.get("GRAPHX_TELEMETRY_SHARED_SECRET_FILE", "")
    if inline and path:
        raise ValueError("telemetry secret and file are mutually exclusive")
    if path:
        with open(path, encoding="utf-8") as stream:
            inline = stream.read(4097).removesuffix("\n").removesuffix("\r")
    if len(inline.encode()) > 4096:
        raise ValueError("telemetry credential exceeds 4096 bytes")
    return inline


def configure_sdr_runtime(role: str) -> None:
    """Adapt authoritative typed settings to the existing raw SDR implementation."""
    context = execution_context()
    if not context:
        return
    config = normalized_config()
    node = next(item for item in config["graph"]["nodes"] if item["id"] == context["nodeId"])
    edges = {item["id"]: item for item in config["graph"]["edges"]}
    if role == "source":
        source = node
    else:
        candidates = [item for item in config["graph"]["nodes"] if item.get("sdr") and
                      edges[item["sdr"]["control_edge"]]["from"]["node"] == node["id"]]
        if len(candidates) != 1:
            raise ValueError("controller must select exactly one configured SDR source")
        source = candidates[0]
    settings = source.get("sdr")
    if not settings:
        raise ValueError("selected source has no typed SDR settings")
    samples = edges[settings["samples_edge"]]["transport"]
    control = edges[settings["control_edge"]]["transport"]
    credentials = settings["credentials"] if role == "source" else control["tls"]
    os.environ.update({
        "SDR_SAMPLE_TARGET": samples["destination"], "SDR_SAMPLE_BIND": samples["bind"],
        "SDR_SAMPLE_PORT": str(samples["port"]), "SDR_CONTROL_PORT": str(control["port"]),
        "SDR_CONTROL_BIND": control["bind"], "SDR_CONTROL_TARGET": control["host"],
        "SDR_SAMPLE_INTERVAL_SECONDS": str(settings["sample_interval_ms"] / 1000),
        "SDR_FREQUENCY_HZ": str(settings["frequency_hz"]),
        "SDR_TLS_CA": credentials["ca_file"], "SDR_TLS_CLIENT_CA": credentials["ca_file"],
        "SDR_TLS_CERT": credentials["certificate_file"], "SDR_TLS_KEY": credentials["private_key_file"],
        "SDR_TLS_SERVER_NAME": credentials["server_name"],
    })
    addresses = [item["address"].split("/")[0] for item in config["network"]["attachments"]
                 if item["owner"] == source["id"] and item.get("address")]
    os.environ["SDR_SAMPLE_SOURCE"] = addresses[0] if len(addresses) == 1 else control["host"]
    if role == "controller":
        results = [edge for edge in edges.values() if edge["from"]["node"] == node["id"] and
                   edge["id"] != settings["control_edge"]]
        if len(results) > 1:
            raise ValueError("SDR controller supports at most one result edge")
        os.environ["SDR_RESULT_LOCAL"] = "0" if results else "1"
        if results:
            os.environ["SDR_RESULT_TARGET"] = results[0]["transport"]["host"]
            os.environ["SDR_RESULT_PORT"] = str(results[0]["transport"]["port"])
