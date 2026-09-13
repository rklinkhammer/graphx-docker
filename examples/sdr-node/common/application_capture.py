"""Bounded PCAPNG observations of raw application bytes (LINKTYPE_USER1).

These are application records, not Ethernet/IP packets or encrypted wire captures.
"""
import atexit
import os
from pathlib import Path
import stat
import struct
import threading
import time


def padded(data):
    return data + b'\0' * (-len(data) % 4)


def option(code, value):
    return struct.pack('<HH', code, len(value)) + padded(value)


def block(kind, body):
    body = padded(body)
    size = 12 + len(body)
    return struct.pack('<II', kind, size) + body + struct.pack('<I', size)


class ApplicationCapture:
    def __init__(self, node):
        config = node['capture']
        self.maximum = config['max_file_bytes']
        self.max_packets = config['max_packets']
        self.snaplen = config['snaplen']
        self.lock = threading.Lock()
        self.count = 0
        self.fd = -1
        directory = Path(config['directory'])
        if not directory.is_absolute():
            raise ValueError('application capture requires an absolute execution path')
        for parent in (directory, *directory.parents):
            if parent.is_symlink(): raise ValueError('capture directories must not be symlinks')
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / (node['node_id'] + '.pcapng')
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_NOFOLLOW | os.O_NONBLOCK, 0o660)
        try:
            metadata = os.fstat(fd)
            if not stat.S_ISREG(metadata.st_mode) or metadata.st_nlink != 1 or metadata.st_uid != os.geteuid():
                raise ValueError('capture must be an owned regular file')
            os.ftruncate(fd, 0)
            os.fchmod(fd, 0o660)
            self.fd = fd
            header = block(0x0a0d0d0a, struct.pack('<IHHq', 0x1a2b3c4d, 1, 0, -1))
            description = b'Raw application bytes; not network-wire capture'
            header += block(1, struct.pack('<HHI', 148, 0, self.snaplen) +
                            option(2, node['node_id'].encode()) + option(3, description) + b'\0' * 4)
            self.size = 0
            self._write(header)
        except BaseException:
            os.close(fd)
            self.fd = -1
            raise
        atexit.register(self.close)

    def _write(self, data):
        offset = 0
        while offset < len(data):
            count = os.write(self.fd, data[offset:])
            if count <= 0: raise OSError('incomplete capture write')
            offset += count
        self.size += len(data)

    def record(self, payload, boundary):
        with self.lock:
            if self.fd < 0 or self.count >= self.max_packets: return
            timestamp = time.time_ns() // 1000
            captured = payload[:self.snaplen]
            fields = struct.pack('<IIIII', 0, timestamp >> 32, timestamp & 0xffffffff,
                                 len(captured), len(payload))
            packet = block(6, fields + padded(captured) + option(1, boundary.encode()[:160]) + b'\0' * 4)
            if self.size + len(packet) > self.maximum: return
            self._write(packet)
            self.count += 1

    def close(self):
        with self.lock:
            if self.fd >= 0:
                os.fsync(self.fd)
                os.close(self.fd)
                self.fd = -1
