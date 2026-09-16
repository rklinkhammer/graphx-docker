#!/usr/bin/env python3
"""Generate radio packets with narrowly scoped fixes to the pinned vrtgen templates."""
import pathlib
import shutil
import subprocess
import sys

out = pathlib.Path(sys.argv[1]).resolve()
profile = pathlib.Path(sys.argv[2]).resolve()
source = pathlib.Path(sys.argv[3]).resolve() / 'src/vrtgen'
patched = out / 'generator' / 'vrtgen'
shutil.copytree(source, patched, dirs_exist_ok=True)
(patched / '__init__.py').touch()
root = patched / 'backend/cpp/templates'
p = root / 'ack_packet.hpp.jinja2'
s = p.read_text()
for i in range(3):
    s = s.replace(f'packet.eif{i}.enabled and packet.warnings_enabled',
                  f'packet.eif{i}.enabled and packet.errors_enabled')
s = s.replace('{{ function_decls.getters_and_setters(packet.cam.action_mode, type_helper) | trim }}',
              '{{ function_decls.cam_getter(packet.cam.action_mode, type_helper) | trim }}')
p.write_text(s)
# Use the installed entry-point metadata with the patched module first on sys.path.
subprocess.run([sys.executable, '-c',
    'import sys; sys.path.insert(0,sys.argv.pop(1)); from vrtgen.main import main; main()',
    str(patched.parent), 'cpp', '--namespace', 'graphx::vita', '--dir', str(out), str(profile)], check=True)
