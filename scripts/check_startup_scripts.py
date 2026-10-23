#!/usr/bin/env python3
"""Verify selected startup/lifecycle scripts in the extracted final rootfs."""
import argparse
import hashlib
import json
from pathlib import Path
import stat

RC_COMMON_SHA256 = '54a393491745cbef8653076b00eddec6a20ff587c4d9b9d1e0814ba8d15f16f3'
SCRIPTS = {
    'usr/bin/aitvbox-start-ui': 'packaging/aitvbox-suite/files/start-ui.sh',
    'etc/init.d/aitvbox-apps': 'packaging/aitvbox-platform/files/aitvbox-apps.init',
    'etc/init.d/aitvbox-control': 'packaging/aitvbox-platform/files/aitvbox-control.init',
    'usr/bin/aitvbox-appctl': 'platform/runtime/aitvbox-appctl',
    'usr/bin/aitvbox-agentctl': 'platform/runtime/aitvbox-agentctl',
}

def digest(path, root, installed=True):
    path.relative_to(root)  # Raises ValueError outside root; supports Python 3.8.
    if path.resolve() != path or not path.is_file():
        raise ValueError('missing or aliased script: ' + str(path))
    mode = path.stat().st_mode
    if not stat.S_ISREG(mode) or (installed and (not mode & 0o111 or mode & 0o022)):
        raise ValueError('unsafe script mode: ' + str(path))
    return hashlib.sha256(path.read_bytes()).hexdigest()

def check(rootfs, product):
    rootfs, product = Path(rootfs).resolve(), Path(product).resolve()
    expected = {'etc/rc.common': RC_COMMON_SHA256}
    for target, source in SCRIPTS.items():
        expected[target] = digest(product / source, product, installed=False)
    for target, value in expected.items():
        if digest(rootfs / target, rootfs) != value:
            raise ValueError('stale startup script: ' + target)
    return dict(status='passed', scripts=expected, release_qualified=False,
                scope='Selected extracted rootfs script bytes/modes only, not runtime acceptance')

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rootfs',required=True);parser.add_argument('--product',required=True)
    args=parser.parse_args()
    try: print(json.dumps(check(args.rootfs,args.product)))
    except (ValueError,OSError) as error: parser.exit(1,'STARTUP SCRIPTS BLOCKED: '+str(error)+'\n')
