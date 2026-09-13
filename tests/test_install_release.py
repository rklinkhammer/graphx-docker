#!/usr/bin/env python3
"""Negative installation acceptance against supplied verified release candidates."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'scripts/release'))
from install_release import install
from release_common import ReleaseError

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('native',type=Path)
parser.add_argument('companion',type=Path)
args=parser.parse_args()
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
epoch=int(subprocess.check_output(['git','show','-s','--format=%ct','HEAD'],cwd=root,text=True))
with tempfile.TemporaryDirectory(prefix='graphx-install-') as temporary:
    parent=Path(temporary).resolve()
    for label,expected_commit,expected_epoch,allow_dirty in [
            ('wrong-commit','0'*40,epoch,True), ('wrong-epoch',commit,epoch+1,True),
            ('dirty-companion',commit,epoch,False)]:
        destination=parent/label
        try: install(args.native,args.companion,destination,expected_commit,'1.1.0',expected_epoch,allow_dirty)
        except ReleaseError: pass
        else: raise AssertionError(label+' was accepted')
        assert not destination.exists()
    existing=parent/'existing';existing.mkdir();(existing/'sentinel').write_text('preserve')
    try: install(args.native,args.companion,existing,commit,'1.1.0',epoch,True)
    except ReleaseError: pass
    else: raise AssertionError('existing installation was accepted')
    assert (existing/'sentinel').read_text()=='preserve'
print('Wrong release commit/epoch, dirty policy and existing installation refused without writes')
