#!/usr/bin/env python3
"""Stage only desktop, Web assets, fonts and application resources."""
import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile
from baseline import digest
from application_build import verify_receipt

ROOT=Path(__file__).resolve().parents[2]
EMOTIONS={'static.json':'Neutral & Thinking','investigate.json':'Neutral & Thinking','ponder.json':'Neutral & Thinking','question.json':'Neutral & Thinking','smile.json':'Positive Emotions','sad.json':'Negative Emotions','angry.json':'Negative Emotions','panic.json':'Surprise & Reactions','shocked.json':'Surprise & Reactions','asleep.json':'Physical & Special States'}

def copy_file(source,target,mode=0o644):
    if source.is_symlink() or not source.is_file():raise ValueError('source must be a regular file: '+str(source))
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(source,target);target.chmod(mode)

def stage(desktop,web,output,receipt):
    if output.exists():raise ValueError('output already exists')
    verified=verify_receipt(receipt,desktop,ROOT)
    if not (web/'index.html').is_file():raise ValueError('Web device build has no index.html')
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.application-',dir=output.parent) as temporary:
        tree=Path(temporary)
        copy_file(desktop,tree/'bin/lvglsim',0o755)
        copy_file(receipt,tree/'resources/application-build.json')
        if digest(tree/'bin/lvglsim')!=verified['desktop_sha256']:
            raise ValueError('desktop changed while staging')
        verify_receipt(tree/'resources/application-build.json',tree/'bin/lvglsim',ROOT)
        fonts=ROOT/'apps/lv_port_linux/src/ui/font'
        for name in ['SarasaUiSC-Regular.ttf','SarasaUiSC-SemiBold.ttf']:
            copy_file(fonts/name,tree/'fonts'/name)
        copy_file(fonts/'LICENSE-Sarasa.txt',tree/'resources/licenses/LICENSE-Sarasa.txt')
        for name,group in EMOTIONS.items():
            copy_file(ROOT/'third_party/aitvbox_emotion_lottie_pack'/group/name,tree/'resources/emotions'/name)
        for path in sorted(web.rglob('*')):
            if path.is_symlink():raise ValueError('Web assets contain a symbolic link')
            if path.is_file():
                if path.suffix in {'.map','.pem','.key','.env'}:raise ValueError('unexpected debug or credential file in Web build')
                copy_file(path,tree/'www'/path.relative_to(web))
        copy_file(ROOT/'docs/ai-emotion-ui.md',tree/'resources/emotions/SOURCE.md')
        copy_file(ROOT/'THIRD_PARTY_NOTICES.md',tree/'resources/THIRD_PARTY_NOTICES.md')
        copy_file(ROOT/'LICENSE',tree/'resources/LICENSE')
        for path in [tree]+[p for p in tree.rglob('*') if p.is_dir()]:path.chmod(0o755)
        os.rename(tree,output)
    return {p.relative_to(output).as_posix():digest(p) for p in sorted(output.rglob('*')) if p.is_file()}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--desktop',required=True,type=Path);p.add_argument('--web',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
    p.add_argument('--build-receipt',required=True,type=Path)
    a=p.parse_args()
    try:result=stage(a.desktop.absolute(),a.web.resolve(),a.output.resolve(),a.build_receipt.absolute())
    except (ValueError,OSError) as e:p.exit(1,'APPLICATION STAGING BLOCKED: '+str(e)+'\n')
    print(json.dumps({'files':len(result),'desktop_sha256':result['bin/lvglsim'],'note':'Unsigned staging only; pack requires a complete target manifest and external signing key.'}))
if __name__=='__main__':main()
