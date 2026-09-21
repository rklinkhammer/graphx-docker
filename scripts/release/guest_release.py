#!/usr/bin/env python3
"""Build verified x86_64 TCG guest artifacts with the reviewed Buildroot recipe."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import uuid
import tarfile
import csv
import datetime as dt
import re
import stat
import tempfile
import gzip
import struct

ROOT = Path(__file__).resolve().parents[2]
GUEST_SOURCES = {
    'usr/lib/graphx/agent.py':'guests/common/agent.py',
    'usr/lib/graphx/sdr/radio.py':'guests/common/radio.py',
    'usr/lib/graphx/sdr/sdr_simulator.py':'examples/sdr-node/common/sdr_simulator.py',
    'etc/init.d/S80graphx':'guests/buildroot-external/board/overlay/etc/init.d/S80graphx'}
sys.path.insert(0, str(ROOT / 'examples/qemu-node/tools'))
from artifact_manifest import digest, tree_digest
from build_trust import build_trust
from release_common import ReleaseError, json_object, validate_commit, validate_epoch, inspect_archive


def write(path, value):
    path.write_text(json.dumps(value, sort_keys=True, indent=2) + '\n')


def sources(source, destination):
    # One reviewed software context; never include local state, credentials or build output.
    for name in ['CMakeLists.txt','VERSION','LICENSE','THIRD_PARTY.md','README.md','SECURITY.md',
                 'SUPPORT.md','CONTRIBUTING.md','cmake','include','src','apps','config','docs','deploy',
                 'tools','wireshark','guests','examples/sdr-node/common']:
        original, target = source/name, destination/name
        if original.is_symlink():
            raise ReleaseError('source context contains a symlink')
        if original.is_dir():
            shutil.copytree(original, target, symlinks=True, ignore=shutil.ignore_patterns('node_modules','dist','__pycache__','*.pyc'))
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original, target)


def fetch_yaml(source, context, archive_path):
    declaration = re.search(r'FetchContent_Declare\(yaml-cpp\s+URL (https://github.com/jbeder/yaml-cpp/archive/[^\s]+)\s+URL_HASH SHA256=([a-f0-9]{64})',
                            (source/'CMakeLists.txt').read_text())
    if not declaration:
        raise ReleaseError('reviewed yaml-cpp URL/checksum declaration not found')
    url, expected = declaration.groups()
    subprocess.run(['curl','--fail','--location','--max-time','120','--max-filesize','16777216',
                    '--output',str(archive_path),url], check=True, timeout=130)
    if digest(archive_path) != expected:
        raise ReleaseError('yaml-cpp archive checksum mismatch')
    inspection = inspect_archive(archive_path, max_expanded_bytes=64*1024*1024)
    prefixes = {Path(name).parts[0] for name in inspection.names}
    if len(prefixes) != 1:
        raise ReleaseError('invalid yaml-cpp source layout')
    prefix = prefixes.pop()
    with tarfile.open(archive_path) as archive:
        for member in archive:
            name = Path(member.name).relative_to(prefix)
            target = context/'deps/yaml-cpp'/name
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with target.open('xb') as stream:
                    shutil.copyfileobj(archive.extractfile(member),stream)
                target.chmod(member.mode & 0o777)
    return {'url':url, 'sha256':expected}


def elf_dependencies(value):
    """Read bounded ELF64 section metadata; never execute a packaged binary."""
    if len(value) < 64 or value[:6] != b'\x7fELF\x02\x01' or value[18:20] != b'\x3e\0':
        raise ReleaseError('guest executable is not x86-64 ELF')
    offset, = struct.unpack_from('<Q', value, 40)
    entry_size, count = struct.unpack_from('<HH', value, 58)
    if count == 0:
        return set()
    if entry_size != 64 or count > 4096 or offset + count*64 > len(value):
        raise ReleaseError('invalid guest ELF section table')
    sections = [struct.unpack_from('<IIQQQQIIQQ', value, offset+i*64) for i in range(count)]
    dependencies = set()
    for section in sections:
        if section[1] != 6:  # SHT_DYNAMIC
            continue
        start, size, link = section[4:7]
        if link >= count or size % 16 or start+size > len(value):
            raise ReleaseError('invalid guest ELF dynamic table')
        strings = sections[link]
        if strings[1] != 3 or strings[4]+strings[5] > len(value):
            raise ReleaseError('invalid guest ELF string table')
        table = value[strings[4]:strings[4]+strings[5]]
        for index in range(start, start+size, 16):
            tag, pointer = struct.unpack_from('<qQ', value, index)
            if tag != 1:  # DT_NEEDED
                continue
            end = table.find(b'\0', pointer)
            if end < 0 or end-pointer > 255:
                raise ReleaseError('invalid guest ELF dependency')
            name = table[pointer:end].decode('ascii')
            if not re.fullmatch(r'[A-Za-z0-9_.+-]+', name) or name.startswith(('libgraphx', 'libyaml')):
                raise ReleaseError('guest executable requires an unbundled application library')
            dependencies.add(name)
    return dependencies


def inspect_initramfs(path, application):
    """Inspect bounded newc bytes without extracting files or trusting declared identities."""
    required = {'usr/bin/graphx', 'usr/bin/graphx-packet-guest', 'usr/lib/graphx/agent.py',
                'usr/lib/graphx/application', 'etc/init.d/S80graphx',
                'usr/lib/graphx/sdr/radio.py', 'usr/lib/graphx/sdr/sdr_simulator.py'}
    found = {}
    total = 0
    names = set()
    directories = set()
    dependencies = set()
    with gzip.open(path, 'rb') as stream:
        def read(size):
            nonlocal total
            total += size
            if total > 512*1024*1024:
                raise ReleaseError('initramfs expanded size exceeds bound')
            value = stream.read(size)
            if len(value) != size:
                raise ReleaseError('truncated initramfs')
            return value
        for _ in range(32768):
            header = read(110)
            if header[:6] != b'070701':
                raise ReleaseError('unsupported initramfs archive format')
            fields = [int(header[6+8*i:14+8*i],16) for i in range(13)]
            mode, size, length = fields[1], fields[6], fields[11]
            if not 1 <= length <= 4096 or size > 128*1024*1024:
                raise ReleaseError('initramfs member exceeds bound')
            raw = read(length)
            if raw[-1:] != b'\0' or b'\0' in raw[:-1]:
                raise ReleaseError('invalid initramfs name')
            name = raw[:-1].decode('utf-8')
            read(-(110+length) % 4)
            if name == 'TRAILER!!!':
                if size:
                    raise ReleaseError('invalid initramfs trailer')
                break
            if name.startswith('./'):
                name = name[2:]
            if name != '.':
                relative(name)
            if name in names:
                raise ReleaseError('duplicate initramfs path')
            names.add(name)
            if stat.S_ISDIR(mode) and not mode & 0o022:
                directories.add(name)
            if name in required:
                if not stat.S_ISREG(mode) or fields[2:4] != [0,0] or (name.startswith(('usr/bin/', 'etc/init.d/')) and not mode & 0o111):
                    raise ReleaseError('guest application member must be regular')
                value = read(size)
                if name.startswith('usr/bin/'):
                    dependencies.update(elf_dependencies(value))
                if name == 'usr/lib/graphx/application' and value != (application+'\n').encode():
                    raise ReleaseError('initramfs application contract mismatch')
                found[name] = hashlib.sha256(value).hexdigest()
            else:
                remaining = size
                while remaining:
                    chunk = min(remaining, 65536)
                    read(chunk)
                    remaining -= chunk
            read(-size % 4)
        else:
            raise ReleaseError('initramfs member count exceeds bound')
        trailing = stream.read(4097)
        if len(trailing) > 4096 or any(trailing):
            raise ReleaseError('unexpected initramfs trailer bytes')
    for dependency in dependencies:
        if not any(directory+dependency in names for directory in ('lib/', 'usr/lib/')):
            raise ReleaseError('initramfs is missing an executable runtime dependency')
    for name in required:
        if any(str(parent) not in directories for parent in Path(name).parents if str(parent) != '.'):
            raise ReleaseError('guest application parent is not a protected directory')
    if set(found) != required:
        raise ReleaseError('initramfs is missing the GraphX application or provisioning agent')
    return found


def build(args):
    source, output = args.source.resolve(), args.output.absolute()
    if any(p.is_symlink() for p in (output, *output.parents)):
        raise ReleaseError('guest output must not contain symlinks')
    output = output.resolve()
    if output.is_relative_to(source):
        raise ReleaseError('guest output must be outside the source tree')
    verify_catalog(args.catalog.absolute())
    if output.exists():
        raise ReleaseError('guest output must be absent')
    dirty = bool(subprocess.check_output(['git','status','--porcelain'],cwd=source))
    if dirty and not args.allow_dirty:
        raise ReleaseError('guest builds require a clean source or explicit --allow-dirty')
    _, trust_arguments = build_trust()
    subprocess.run(['docker','info'],check=True,stdout=subprocess.DEVNULL)
    if sys.platform == 'darwin' and subprocess.check_output(['docker','context','show'],text=True).strip() != 'orbstack':
        raise ReleaseError('macOS guest builds require OrbStack')
    output.mkdir(parents=True)
    context = output/'source'
    sources(source, context)
    yaml_input = fetch_yaml(source, context, output/'yaml-cpp.tar.gz')
    if any(p.is_symlink() for p in context.rglob('*')):
        raise ReleaseError('source context contains a symlink')
    source_sha = tree_digest(context)
    tag = 'graphx-guest-builder-' + uuid.uuid4().hex
    subprocess.run(['docker','build','-f',str(source/'examples/qemu-node/build-env/Dockerfile'),
                    '-t',tag,*trust_arguments,str(source)],check=True,timeout=1800)
    image = json.loads(subprocess.check_output(['docker','image','inspect',tag],text=True))[0]['Id']
    saved = output/'builder.oci.tar'
    subprocess.run(['docker','image','save','-o',str(saved),tag],check=True)
    with tarfile.open(saved) as archive:
        descriptor = json.load(archive.extractfile('index.json'))['manifests'][0]
        builder_digest = descriptor['digest']
        manifest_bytes = archive.extractfile('blobs/sha256/'+builder_digest.split(':')[1]).read()
        if 'sha256:'+hashlib.sha256(manifest_bytes).hexdigest() != builder_digest:
            raise ReleaseError('builder manifest checksum mismatch')
    epoch = int(subprocess.check_output(['git','show','-s','--format=%ct','HEAD'],cwd=source))
    commit = subprocess.check_output(['git','rev-parse','HEAD'],cwd=source,text=True).strip()
    shutil.copytree(args.catalog, output/'catalog')
    shutil.copyfile(source/'config/catalog/sources.json', output/'catalog/sources.json')
    lock = json.loads((output/'catalog/lock.json').read_text())
    work = args.work_dir.absolute() if args.work_dir else output/'build'
    if args.work_dir and not args.allow_dirty:
        raise ReleaseError('reusing a development build cache requires --allow-dirty')
    if any(p.is_symlink() for p in (work, *work.parents)):
        raise ReleaseError('build work root must not be a symlink')
    work.mkdir(parents=True, exist_ok=True)
    for role, application in [('echo','guest.echo'),('radio','sdr.radio')]:
        recipe_id = role+'-x86'
        argv = ['make','-C','/opt/buildroot','O=/work','BR2_EXTERNAL=/source/guests/buildroot-external',
                'graphx_'+role+'_x86_defconfig']
        environment = ['--env','HOME=/tmp','--env','SOURCE_DATE_EPOCH='+str(epoch)]
        configuration_check = " && grep -qx 'BR2_PACKAGE_GRAPHX_GUEST=y' /work/.config && grep -qx 'BR2_PACKAGE_GRAPHX_GUEST_APPLICATION=\""+application+"\"' /work/.config"
        build_script = ' '.join(argv)+configuration_check+' && make -C /opt/buildroot O=/work graphx-guest-dirclean && make -C /opt/buildroot O=/work BR2_DL_DIR=/work/dl -j4 && make -C /opt/buildroot O=/work BR2_DL_DIR=/work/dl legal-info'
        prefix = ['docker','run','--rm','--user',str(os.getuid())+':'+str(os.getgid()),
                   *environment,'--mount','type=bind,src='+str(context)+',dst=/source,readonly',
                   '--mount','type=bind,src='+str(work)+',dst=/work']
        fetch_script = ' '.join(argv)+configuration_check+' && make -C /opt/buildroot O=/work BR2_DL_DIR=/work/dl source'
        subprocess.run([*prefix,image,'-lc',fetch_script],check=True,timeout=3600)
        subprocess.run([*prefix,'--network','none',image,'-lc',build_script],check=True,timeout=14400)
        destination = output/'guests'/recipe_id
        destination.mkdir(parents=True)
        for name in ('bzImage','rootfs.cpio.gz'):
            shutil.copyfile(work/'images'/name,destination/name)
        guest_files = inspect_initramfs(destination/'rootfs.cpio.gz', application)
        for installed, original in GUEST_SOURCES.items():
            if guest_files[installed] != digest(context/original):
                raise ReleaseError('packaged guest software differs from its source context')
        legal = work/'legal-info'
        licenses = {'format':'buildroot-legal-info', 'files':{str(p.relative_to(legal)):digest(p)
                      for p in sorted(legal.rglob('*')) if p.is_file()}}
        with (legal/'manifest.csv').open() as stream:
            licenses['packages'] = list(csv.DictReader(stream))
        licenses['spdx'] = {
            'spdxVersion':'SPDX-2.3', 'dataLicense':'CC0-1.0', 'SPDXID':'SPDXRef-DOCUMENT',
            'name':'graphx-guest-'+recipe_id,
            'documentNamespace':'https://graphx.invalid/guest/'+recipe_id+'/'+source_sha,
            'creationInfo':{'created':dt.datetime.fromtimestamp(epoch, dt.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
                            'creators':['Tool: graphx-guest-release/1']},
            'packages':[{'SPDXID':'SPDXRef-Package-'+str(index), 'name':package['PACKAGE'],
                         'versionInfo':package['VERSION'], 'downloadLocation':'NOASSERTION',
                         'filesAnalyzed':False, 'licenseConcluded':'NOASSERTION', 'licenseDeclared':'NOASSERTION',
                         'licenseComments':'Buildroot declaration: '+package['LICENSE']}
                        for index, package in enumerate(licenses['packages'])],
            'relationships':[{'spdxElementId':'SPDXRef-DOCUMENT', 'relationshipType':'DESCRIBES',
                              'relatedSpdxElement':'SPDXRef-Package-'+str(index)}
                             for index in range(len(licenses['packages']))]} 
        write(destination/'licenses.json',licenses)
        recipe_path = output/'catalog/guests'/(recipe_id+'.json')
        recipe = json.loads(recipe_path.read_text())
        recipe.update(exists_today=True, source_digest=source_sha, source_date_epoch=epoch,
                      builder_image='graphx-guest-builder@'+builder_digest, build_argv=['/bin/bash','-lc',build_script],
                      required_work='Verified artifact set; guest boot requires separate authorized acceptance')
        manifest = {key:recipe[key] for key in ['id','application','architecture','recipe_revision',
                    'source_digest','source_date_epoch','builder_image','build_argv']}
        manifest.update(commit=commit,dirty_candidate=dirty, build_network='none', dependency_fetch_argv=['/bin/bash','-lc',fetch_script], yaml_cpp=yaml_input, guest_files=guest_files, artifacts={name:digest(destination/name)
                         for name in ('bzImage','rootfs.cpio.gz','licenses.json')})
        write(destination/'artifact-manifest.json',manifest)
        recipe['outputs']=[{'path':p.name,'sha256':digest(p)} for p in sorted(destination.iterdir())]
        write(recipe_path,recipe)
    if tree_digest(context) != source_sha:
        raise ReleaseError('source context changed during build')
    for item in lock['files']:
        item['sha256'] = digest(output/'catalog'/item['path'])
    write(output/'catalog/lock.json',lock)
    verify(output, args.allow_dirty)
    print('Guest artifacts and catalog:',output)


ARTIFACTS = {'bzImage', 'rootfs.cpio.gz', 'licenses.json', 'artifact-manifest.json'}
PROVENANCE = ('id', 'application', 'architecture', 'recipe_revision', 'source_digest',
              'source_date_epoch', 'builder_image', 'build_argv')


def regular(path, limit=512*1024*1024):
    for item in (path, *path.parents):
        if item.is_symlink():
            raise ReleaseError('symlink in release path')
    info = path.stat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or not 0 < info.st_size <= limit:
        raise ReleaseError('unsafe or oversized release file')
    return path


def document(path):
    return json_object(regular(path, 1024*1024), 1024*1024, 'guest release document')


def relative(name):
    if not isinstance(name, str) or not name or Path(name).is_absolute() or any(
            part in ('', '.', '..') for part in name.split('/')) or '\\' in name:
        raise ReleaseError('unsafe release member path')
    return name


def verify_catalog(catalog):
    lock = document(catalog/'lock.json')
    names = set()
    for item in lock['files']:
        name = relative(item['path'])
        if name in names or digest(regular(catalog/name)) != item['sha256']:
            raise ReleaseError('guest catalog lock mismatch')
        names.add(name)
    actual = {str(p.relative_to(catalog)) for p in catalog.rglob('*') if not p.is_dir()}
    if (catalog/'README.md').exists():
        regular(catalog/'README.md', 1024*1024)
        actual.remove('README.md')
    if actual != names | {'lock.json'}:
        raise ReleaseError('guest catalog inventory mismatch')
    return lock


def verify(guests, allow_dirty=False):
    catalog = guests/'catalog'
    lock = verify_catalog(catalog)
    names = {item['path'] for item in lock['files']}
    results = {}
    for recipe_name in sorted(name for name in names if name.startswith('guests/')):
        recipe = document(catalog/recipe_name)
        identifier = recipe['id']
        if identifier not in ('echo-x86', 'radio-x86') or recipe_name != 'guests/'+identifier+'.json':
            raise ReleaseError('unsupported guest recipe identity')
        if recipe['exists_today'] is not True or recipe['architecture'] != 'x86_64' or recipe['accelerators'] != ['tcg']:
            raise ReleaseError('guest unavailable or incompatible architecture')
        if recipe['application'] != {'echo-x86':'guest.echo', 'radio-x86':'sdr.radio'}[identifier]:
            raise ReleaseError('guest application mismatch')
        validate_epoch(recipe['source_date_epoch'])
        if not re.fullmatch('[0-9a-f]{64}', recipe['source_digest']) or not re.fullmatch(
                'graphx-guest-builder@sha256:[0-9a-f]{64}', recipe['builder_image']):
            raise ReleaseError('invalid guest source or builder pin')
        directory = guests/'guests'/identifier
        if {p.name for p in directory.iterdir()} != ARTIFACTS:
            raise ReleaseError('guest artifact inventory mismatch')
        outputs = {item['path']:item['sha256'] for item in recipe['outputs']}
        if len(outputs) != len(recipe['outputs']) or set(outputs) != ARTIFACTS:
            raise ReleaseError('invalid guest output inventory')
        for name, checksum in outputs.items():
            if digest(regular(directory/name)) != checksum:
                raise ReleaseError('guest artifact checksum mismatch')
        manifest = document(directory/'artifact-manifest.json')
        if any(manifest[key] != recipe[key] for key in PROVENANCE):
            raise ReleaseError('guest provenance mismatch')
        validate_commit(manifest['commit'])
        if manifest.get('build_network') != 'none':
            raise ReleaseError('guest compile/package network must be disabled')
        if type(manifest['dirty_candidate']) is not bool or (manifest['dirty_candidate'] and not allow_dirty):
            raise ReleaseError('dirty guest requires explicit --allow-dirty')
        if manifest['artifacts'] != {name:outputs[name] for name in ARTIFACTS-{'artifact-manifest.json'}}:
            raise ReleaseError('guest manifest output mismatch')
        # Linux boot protocol magic and x86-64 capability bit; a checksum alone is not architecture evidence.
        with (directory/'bzImage').open('rb') as stream:
            header = stream.read(0x238)
        if len(header) < 0x238 or header[0x202:0x206] != b'HdrS' or not header[0x236] & 1:
            raise ReleaseError('kernel is not an x86-64 Linux boot image')
        actual_guest_files = inspect_initramfs(directory/'rootfs.cpio.gz', recipe['application'])
        if manifest.get('guest_files') != actual_guest_files:
            raise ReleaseError('guest filesystem manifest mismatch')
        licenses = document(directory/'licenses.json')
        if not any(p.get('PACKAGE') == 'graphx-guest' for p in licenses.get('packages', [])) or not licenses.get('files') or licenses.get('spdx', {}).get('spdxVersion') != 'SPDX-2.3':
            raise ReleaseError('guest license inventory missing')
        results[identifier] = (recipe, manifest)
    if set(results) != {'echo-x86', 'radio-x86'}:
        raise ReleaseError('incomplete guest release')
    if (guests/'source').exists():
        if any(p.is_symlink() for p in (guests/'source').rglob('*')):
            raise ReleaseError('guest source context contains symlinks')
        source_sha = tree_digest(guests/'source')
        for _, manifest in results.values():
            if any(manifest['guest_files'][installed] != digest(regular(guests/'source'/original))
                   for installed, original in GUEST_SOURCES.items()):
                raise ReleaseError('packaged guest source mismatch')
        if any(recipe['source_digest'] != source_sha for recipe, _ in results.values()):
            raise ReleaseError('guest source context mismatch')
    if {p.name for p in (guests/'guests').iterdir()} != set(results):
        raise ReleaseError('unexpected guest directory')
    return results


def install(args):
    source, output = args.native.absolute(), args.output.absolute()
    if output.exists() or output.is_symlink() or any(p.is_symlink() for p in output.parents):
        raise ReleaseError('installation must be absent with real parent directories')
    receipt = document(source/'release.json')
    actual = {str(p.relative_to(source)) for p in source.rglob('*') if not p.is_dir()} - {'release.json'}
    if actual != set(receipt['files']):
        raise ReleaseError('native inventory mismatch')
    for name, value in receipt['files'].items():
        p = regular(source/relative(name))
        if digest(p) != value['sha256'] or p.stat().st_mode & 0o777 != value['mode']:
            raise ReleaseError('native receipt mismatch')
    guests = args.guests.absolute()
    recipes = verify(guests, args.allow_dirty)
    if any(manifest['commit'] != receipt['commit'] for _, manifest in recipes.values()):
        raise ReleaseError('native and guest source commits differ')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.graphx-guest-install-', dir=output.parent) as raw:
        staging = Path(raw)/'release'
        shutil.copytree(source, staging)
        for identifier, (recipe, _) in recipes.items():
            for item in recipe['outputs']:
                name = 'guests/'+identifier+'/'+item['path']
                if name in receipt['files']:
                    raise ReleaseError('overlapping guest/native inventory')
                target = staging/name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(guests/name, target)
                target.chmod(0o444)
                if digest(target) != item['sha256']:
                    raise ReleaseError('guest changed during installation')
                receipt['files'][name] = {'sha256':item['sha256'], 'mode':0o444}
        for name, item in receipt['files'].items():
            target = regular(staging/name)
            if digest(target) != item['sha256'] or target.stat().st_mode & 0o777 != item['mode']:
                raise ReleaseError('release changed during installation')
        write(staging/'release.json', receipt)
        # Refuse an output race rather than replace any existing installation.
        output.mkdir(mode=0o700)
        try:
            for item in staging.iterdir():
                item.rename(output/item.name)
        except Exception:
            raise ReleaseError('installation publication failed; incomplete output retained')
    print('Combined native/guest installation:', output)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='action',required=True)
    b=sub.add_parser('build')
    b.add_argument('--source',type=Path,default=ROOT)
    b.add_argument('--catalog',type=Path,required=True)
    b.add_argument('--output',type=Path,required=True)
    b.add_argument('--allow-dirty',action='store_true')
    b.add_argument('--work-dir',type=Path,help='Reuse a guest-local development Buildroot cache; requires --allow-dirty')
    v=sub.add_parser('verify')
    v.add_argument('--guests',type=Path,required=True)
    v.add_argument('--allow-dirty',action='store_true')
    i=sub.add_parser('install')
    i.add_argument('--native',type=Path,required=True)
    i.add_argument('--guests',type=Path,required=True)
    i.add_argument('--output',type=Path,required=True)
    i.add_argument('--allow-dirty',action='store_true')
    args=parser.parse_args()
    try:
        if args.action == 'verify':
            verify(args.guests.absolute(), args.allow_dirty)
            print('Guest release verified')
        else:
            (build if args.action=='build' else install)(args)
    except (ReleaseError, ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as error:
        print('guest-release:',error,file=sys.stderr)
        raise SystemExit(2)
