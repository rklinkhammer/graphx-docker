#!/usr/bin/env python3
"""Reject guest release tampering before publishing an installation."""
import json
import gzip
import struct
from pathlib import Path
import shutil
import sys
import tempfile
from types import SimpleNamespace
import unittest

root = Path(sys.argv.pop(1)).resolve()
sys.path.insert(0, str(root/'scripts/release'))
import guest_release as release


def initramfs(path, application, omit=None, architecture=b'\x3e\0', dependency=None):
    elf = bytearray(b'\x7fELF\x02\x01' + bytes(12) + architecture + bytes(44))
    if dependency:
        struct.pack_into('<Q', elf, 40, 64)
        struct.pack_into('<HH', elf, 58, 64, 2)
        elf += struct.pack('<IIQQQQIIQQ', 0, 6, 0, 0, 192, 16, 1, 0, 8, 16)
        name = dependency.encode()+b'\0'
        elf += struct.pack('<IIQQQQIIQQ', 0, 3, 0, 0, 208, len(name), 0, 0, 1, 0)
        elf += struct.pack('<qQ', 1, 0)+name
    files = {'usr/bin/graphx':elf, 'usr/bin/graphx-packet-guest':elf,
             'usr/lib/graphx/agent.py':b'synthetic-agent', 'usr/lib/graphx/application':(application+'\n').encode(),
             'etc/init.d/S80graphx':b'synthetic-init', 'usr/lib/graphx/sdr/radio.py':b'synthetic-radio',
             'usr/lib/graphx/sdr/sdr_simulator.py':b'synthetic-sdr'}
    if omit:
        del files[omit]
    archive = bytearray()
    directories = sorted({str(parent) for name in files for parent in Path(name).parents if str(parent) != '.'})
    for index, (name, data) in enumerate([(name,b'') for name in directories]+[*files.items(), ('TRAILER!!!', b'')]):
        mode = 0o040755 if name in directories else 0o100755
        name = name.encode()+b'\0'
        fields = [index,mode,0,0,1,0,len(data),0,0,0,0,len(name),0]
        archive += b'070701'+b''.join(f'{field:08x}'.encode() for field in fields)+name
        archive += bytes(-len(archive)%4)
        archive += data+bytes(-len(data)%4)
    path.write_bytes(gzip.compress(archive,mtime=0))


class GuestRelease(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name).resolve()
        self.guests = self.path/'candidate'
        shutil.copytree(root/'config/catalog', self.guests/'catalog')
        for identifier in ('echo-x86', 'radio-x86'):
            directory = self.guests/'guests'/identifier
            directory.mkdir(parents=True)
            recipe_path = self.guests/'catalog/guests'/(identifier+'.json')
            recipe = json.loads(recipe_path.read_text())
            recipe.update(exists_today=True, builder_image='graphx-guest-builder@sha256:'+'a'*64)
            header = bytearray(0x238)
            header[0x202:0x206] = b'HdrS'
            header[0x236] = 1
            (directory/'bzImage').write_bytes(header)  # Synthetic protocol header, not a bootable guest.
            initramfs(directory/'rootfs.cpio.gz', recipe['application'])
            release.write(directory/'licenses.json', {'files':{'test':'a'*64}, 'packages':[{'PACKAGE':'graphx-guest'}], 'spdx':{'spdxVersion':'SPDX-2.3'}})
            manifest = {key:recipe[key] for key in release.PROVENANCE}
            manifest.update(commit='b'*40, dirty_candidate=True, build_network='none',
                            guest_files=release.inspect_initramfs(directory/'rootfs.cpio.gz', recipe['application']),
                            artifacts={name:release.digest(directory/name) for name in release.ARTIFACTS-{'artifact-manifest.json'}})
            release.write(directory/'artifact-manifest.json', manifest)
            recipe['outputs'] = [{'path':name, 'sha256':release.digest(directory/name)} for name in sorted(release.ARTIFACTS)]
            release.write(recipe_path, recipe)
        self.lock()

    def tearDown(self):
        self.temp.cleanup()

    def lock(self):
        path = self.guests/'catalog/lock.json'
        lock = json.loads(path.read_text())
        for item in lock['files']:
            item['sha256'] = release.digest(self.guests/'catalog'/item['path'])
        release.write(path, lock)

    def test_artifact_and_lock_tampering(self):
        self.assertEqual(set(release.verify(self.guests, True)), {'echo-x86','radio-x86'})
        with self.assertRaises(release.ReleaseError):
            release.verify(self.guests)
        kernel = self.guests/'guests/echo-x86/bzImage'
        original = kernel.read_bytes()
        kernel.write_bytes(b'tampered')
        with self.assertRaises(release.ReleaseError):
            release.verify(self.guests, True)
        kernel.write_bytes(original)
        backup = self.path/'kernel'
        kernel.rename(backup)
        kernel.symlink_to(backup)
        with self.assertRaises(release.ReleaseError):
            release.verify(self.guests, True)

    def test_catalog_path_and_inventory(self):
        recipe_path = self.guests/'catalog/guests/echo-x86.json'
        recipe = json.loads(recipe_path.read_text())
        for change in ({'id':'../../escape'}, {'architecture':'arm64'}, {'application':'sdr.radio'},
                       {'outputs':[{'path':'../../escape','sha256':'a'*64}]}):
            release.write(recipe_path, {**recipe, **change})
            self.lock()
            with self.assertRaises(release.ReleaseError):
                release.verify(self.guests, True)
        release.write(recipe_path, recipe)
        self.lock()
        (self.guests/'guests/echo-x86/extra').write_text('unexpected')
        with self.assertRaises(release.ReleaseError):
            release.verify(self.guests, True)

    def test_initramfs_contents(self):
        archive = self.path/'initramfs.gz'
        for options in ({'omit':'usr/lib/graphx/agent.py'}, {'architecture':b'\xb7\0'},
                        {'dependency':'libgraphx.so'}, {'dependency':'libmissing.so'}):
            initramfs(archive, 'guest.echo', **options)
            with self.assertRaises(release.ReleaseError):
                release.inspect_initramfs(archive, 'guest.echo')
        initramfs(archive, 'sdr.radio')
        with self.assertRaises(release.ReleaseError):
            release.inspect_initramfs(archive, 'guest.echo')
        archive.write_bytes(gzip.compress(b'not a cpio archive'))
        with self.assertRaises(release.ReleaseError):
            release.inspect_initramfs(archive, 'guest.echo')

    def test_install_failure_has_no_output(self):
        native = self.path/'native'
        native.mkdir()
        (native/'executable').write_text('synthetic-native')
        release.write(native/'release.json', {'commit':'b'*40, 'files':{
            'executable':{'sha256':release.digest(native/'executable'), 'mode':0o644}}})
        args = SimpleNamespace(native=native, output=self.path/'installed', guests=self.guests, allow_dirty=True)
        (self.guests/'guests/echo-x86/bzImage').write_text('bad')
        with self.assertRaises(release.ReleaseError):
            release.install(args)
        self.assertFalse(args.output.exists())
        receipt = json.loads((native/'release.json').read_text())
        receipt['files']['../escape'] = receipt['files'].pop('executable')
        release.write(native/'release.json', receipt)
        with self.assertRaises(release.ReleaseError):
            release.install(args)
        self.assertFalse(args.output.exists())


unittest.main()
