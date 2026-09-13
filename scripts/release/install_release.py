#!/usr/bin/env python3
"""Install verified native and companion archives into one exclusive release root."""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile
from platform_bundle import verify as verify_platform
from release_common import ReleaseError, current_platform
from verify_release import verify_candidate


def install(native, companion, output, commit, version, epoch, allow_dirty=False):
    platform = current_platform()
    verify_candidate(native, 'v'+version, commit, platform, epoch)
    stem = f'graphx-platform-{version}-{platform}'
    manifest = verify_platform(companion/(stem+'.tar.gz'), companion/(stem+'.manifest.json'),
        {'commit':commit, 'graphx_version':version, 'platform':platform, 'epoch':epoch})
    if manifest.get('dirty_candidate') and not allow_dirty:
        raise ReleaseError('dirty companion requires explicit --allow-dirty')
    if output.exists() or output.is_symlink(): raise ReleaseError('installation root must be absent')
    for parent in output.absolute().parents:
        if parent.is_symlink(): raise ReleaseError('installation parents must not be symlinks')
    output.mkdir(parents=True,mode=0o700)
    files = {}
    # Both archive verifiers have checked their exact, bounded, regular inventories.
    for archive in [native/f'graphx-{version}-{platform}.tar.gz', companion/(stem+'.tar.gz')]:
        with tarfile.open(archive) as stream:
            for item in stream:
                if not item.isfile(): continue
                relative = str(Path(item.name).relative_to(f'graphx-{version}-{platform}'))
                if relative in files: raise ReleaseError('archives have overlapping file inventories')
                destination = output/relative
                destination.parent.mkdir(parents=True,exist_ok=True)
                data = stream.extractfile(item).read()
                with destination.open('xb') as target: target.write(data)
                destination.chmod(item.mode & 0o777)
                files[relative] = {'sha256':hashlib.sha256(data).hexdigest(),'mode':item.mode & 0o777}
    (output/'release.json').write_text(json.dumps({'version':1,'graphx_version':version,
        'commit':commit,'platform':platform,'files':files},sort_keys=True,indent=2)+'\n')
    return output


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('native','companion','output'): parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--commit',required=True)
    parser.add_argument('--version',required=True)
    parser.add_argument('--epoch',type=int,required=True)
    parser.add_argument('--allow-dirty',action='store_true')
    args=parser.parse_args()
    print(install(args.native,args.companion,args.output,args.commit,args.version,args.epoch,args.allow_dirty))
