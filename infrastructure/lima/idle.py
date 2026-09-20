#!/usr/bin/env python3
"""Read-only, conservative idle gate for the dedicated GraphX guest.

Exit zero only when no container, laboratory network or application remains.
Unknown inventory failures retain the VM. Never delete resources here.
"""
import json
import os
from pathlib import Path
import subprocess
import sys


def output(*command):
    return subprocess.check_output(command, text=True, timeout=10).strip()


def idle(proc=Path('/proc')):
    for command in [('docker', 'ps', '-aq'), ('ovs-vsctl', '--timeout=5', 'list-br'),
                    ('ip', 'netns', 'list')]:
        if output(*command):
            return False
    links = json.loads(output('ip', '-j', '-d', 'link', 'show'))
    for link in links:
        if link.get('linkinfo', {}).get('info_kind') in ('veth', 'tun'):
            return False
    if 'graphx' in output('nft', 'list', 'tables').lower():
        return False
    if any(item['kind'] == 'netem' for item in json.loads(output('tc', '-j', 'qdisc', 'show'))):
        return False
    for directory in proc.iterdir():
        if not directory.name.isdigit() or int(directory.name) == os.getpid():
            continue
        try:
            arguments = (directory / 'cmdline').read_bytes().replace(b'\0', b' ').decode(errors='replace')
            executable = (directory / 'exe').readlink().name
        except FileNotFoundError:
            continue  # Kernel thread or a process that exited during inspection.
        if ('/var/lib/graphx/' in arguments or '/workspace/graphx-docker/' in arguments
                or executable.startswith(('graphx', 'qemu-system', 'dumpcap', 'tshark'))
                or executable in ('node', 'npm')):
            return False
    return True


if __name__ == '__main__':
    try:
        raise SystemExit(0 if idle() else 1)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'Cannot verify guest idle state: {error}', file=sys.stderr)
        raise SystemExit(2)
