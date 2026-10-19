#!/usr/bin/env python3
"""Root-test signed installer -> non-root real LVGL page, no production writes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import pwd
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run(argv, **kwargs):
    return subprocess.run([str(x) for x in argv],check=True,capture_output=True,text=True,**kwargs)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--user',required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();assert os.geteuid()==0
    user=pwd.getpwnam(a.user);assert user.pw_uid!=0
    out=a.output.resolve();assert out.is_dir()
    review=ROOT/'build/ui-review/review';assert review.is_file()
    with tempfile.TemporaryDirectory(prefix='mimi-app-catalog-',dir='/run') as tmp:
        base=Path(tmp);base.chmod(0o755)
        private=base/'private';private.mkdir(mode=0o700)
        apps=base/'apps';apps.mkdir(mode=0o755)
        script=private/'platform/services/mimi-ubuntu-apps/installer.py'
        parser=private/'platform/tools/aitapp/aitapp.py'
        for dst,src in [(script,ROOT/'services/mimi-ubuntu-apps/installer.py'),(parser,ROOT/'tools/aitapp/aitapp.py')]:
            dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst);dst.chmod(0o644)
        key=private/'fixture.key';pub=private/'fixture.pem'
        run(['openssl','genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:2048','-out',key])
        run(['openssl','pkey','-in',key,'-pubout','-out',pub])
        src=private/'source';(src/'ui').mkdir(parents=True)
        manifest={'schema':1,'id':'com.mimi.notes','name':'Signed Notes','version':'1.0.0','publisher':'ephemeral-test','permissions':[],'ui':{'entry':'ui/main.json','presentation':'embedded'}}
        (src/'manifest.json').write_text(json.dumps(manifest))
        (src/'ui/main.json').write_text(json.dumps({'schema':1,'title':'Signed Notes','layout':{'type':'column','children':[{'type':'text','text':'Verified package page visible'}]}}))
        pkg=private/'fixture.aitapp'
        run(['python3',parser,'build',src,'--key',key,'--output',pkg])
        cfg=private/'installer.json';cfg.write_text(json.dumps({'schema':1,'apps_root':str(apps),'trusted_keys':[str(pub)]}));cfg.chmod(0o600)
        installed=json.loads(run(['python3',script,'--config',cfg,'install',pkg]).stdout)
        ui=base/'desktop.conf';ui.write_text('user_apps.root='+str(apps)+'\nuser_apps.socket_path=\n');ui.chmod(0o644)
        ppm=out/'signed-catalog.ppm'
        r=run(['runuser','-u',a.user,'--','env','LV_PORT_LINUX_CONFIG='+str(ui),'MIMI_REVIEW_APP_NAME=Signed Notes','MIMI_REVIEW_APP_TEXT=Verified package page visible',review,'user-signed-catalog','en','light',ppm,'800','480'])
        (out/'signed-catalog.log').write_text(r.stdout+r.stderr)
        from PIL import Image
        image=out/'signed-catalog.png';Image.open(ppm).save(image);ppm.unlink()
        result={'status':'passed','scope':'Actual signed test package installed by root-controlled CLI; real LVGL client selected/reopened installed page as non-root user','user_uid':user.pw_uid,'installed':installed,'package_sha256':hashlib.sha256(pkg.read_bytes()).hexdigest(),'desktop_fixture_sha256':hashlib.sha256(review.read_bytes()).hexdigest(),'screenshot_sha256':hashlib.sha256(image.read_bytes()).hexdigest(),'production_modified':False,'ephemeral_keys':'removed with isolated root fixture; not product trust','runtime_storage_acceptance':'not_run','physical_acceptance':'not_run'}
        (out/'signed-catalog.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result))


if __name__=='__main__':main()
