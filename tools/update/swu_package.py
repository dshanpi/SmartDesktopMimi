#!/usr/bin/env python3
"""Build A133 A/B SWU without copying private keys into product/SDK trees."""
import argparse
import gzip
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from baseline import digest
from release_identity import valid_version

ROOT = Path(__file__).resolve().parents[2]


def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def external_secret(path, excluded):
    path=path.absolute()
    if path.is_symlink() or not path.is_file() or path.stat().st_mode & 0o077:
        raise ValueError('signing material must be a private regular file (0600 or stricter)')
    real=path.resolve()
    if any(real==r.resolve() or r.resolve() in real.parents for r in excluded):
        raise ValueError('signing secrets must stay outside product, SDK and output trees')
    return real


def package(kernel, rootfs, template, key, public, password, output, version, epoch, excluded):
    if not valid_version(version) or epoch < 0:
        raise ValueError('invalid system version or source epoch')
    if output.exists() or output.is_symlink():
        raise ValueError('output already exists')
    for path in [kernel,rootfs,template,public]:
        if path.is_symlink() or not path.is_file() or path.stat().st_size==0:
            raise ValueError('missing or non-regular image/template/public key')
    key=external_secret(key,[ROOT,output,*excluded])
    if password:password=external_secret(password,[ROOT,output,*excluded])
    pass_args=['-passin','file:'+str(password)] if password else ['-passin','pass:']
    # Compare public DER and require the project minimum RSA strength without
    # displaying any private key or passphrase in command output.
    derived=subprocess.check_output(['openssl','pkey','-in',str(key),*pass_args,'-pubout','-outform','DER'])
    expected=subprocess.check_output(['openssl','pkey','-pubin','-in',str(public),'-outform','DER'])
    if derived != expected:raise ValueError('SWUpdate public/private key mismatch')
    modulus=subprocess.check_output(['openssl','rsa','-pubin','-in',str(public),'-modulus','-noout'],stderr=subprocess.PIPE).decode().strip()
    if not modulus.startswith('Modulus=') or int(modulus.split('=',1)[1],16).bit_length()<3072:
        raise ValueError('SWUpdate requires RSA-3072 or stronger')
    inputs={name:digest(path) for name,path in [('kernel',kernel),('rootfs',rootfs),('template',template),('public_key',public)]}
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.mimi-swu-',dir=output.parent) as temporary:
        stage=Path(temporary);payload=stage/'payload';payload.mkdir()
        shutil.copyfile(kernel,payload/'kernel')
        with rootfs.open('rb') as source,(payload/'rootfs').open('wb') as raw:
            with gzip.GzipFile(filename='',mode='wb',fileobj=raw,mtime=0) as zipped:
                shutil.copyfileobj(source,zipped,1<<20)
        if digest(payload/'kernel') != inputs['kernel']:
            raise ValueError('kernel changed while copying')
        description=template.read_text()
        if description.count('"@kernel"')!=2 or description.count('"@rootfs"')!=2:
            raise ValueError('unexpected A/B description template')
        description,count=re.subn(r'(?m)^(\s*version\s*=\s*)"[^"]+";',lambda m:m[1]+'"'+version+'";',description)
        if count!=1:raise ValueError('missing or ambiguous system version')
        description=description.replace('"@kernel"','"'+digest(payload/'kernel')+'"').replace('"@rootfs"','"'+digest(payload/'rootfs')+'"')
        (payload/'sw-description').write_text(description)
        run(['openssl','dgst','-sha256','-sign',key,*pass_args,'-out',payload/'sw-description.sig',payload/'sw-description'])
        run(['openssl','dgst','-sha256','-verify',public,'-signature',payload/'sw-description.sig',payload/'sw-description'],stdout=subprocess.DEVNULL)
        names=['sw-description','sw-description.sig','kernel','rootfs']
        for name in names:
            p=payload/name;p.chmod(0o644);os.utime(p,(epoch,epoch))
        bundle=stage/'release';bundle.mkdir()
        # Preserve the vendor's crc CPIO format and mandatory signature order.
        with (bundle/'system.swu').open('wb') as archive:
            run(['cpio','--quiet','-o','-H','crc','--reproducible','--owner=0:0'],input=('\n'.join(names)+'\n').encode(),cwd=payload,stdout=archive)
        with (bundle/'system.swu').open('rb') as archive:
            listing=subprocess.check_output(['cpio','--quiet','-it'],stdin=archive).decode().splitlines()
        if listing != names:raise ValueError('SWU member order differs from signed payload')
        for name,path in [('kernel',kernel),('rootfs',rootfs),('template',template),('public_key',public)]:
            if digest(path)!=inputs[name]:raise ValueError('image inputs changed during packaging')
        record=dict(schema_version=1,kind='swu-build',version=version,source_date_epoch=epoch,inputs=inputs,
                    payloads={name:dict(sha256=digest(payload/name),bytes=(payload/name).stat().st_size) for name in names},
                    package_sha256=digest(bundle/'system.swu'),package_bytes=(bundle/'system.swu').stat().st_size,
                    validation='host signature and archive only; device swupdate -c and upgrade acceptance still required')
        (bundle/'swu-build.json').write_text(json.dumps(record,indent=2,sort_keys=True)+'\n')
        bundle.rename(output)
    return record


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['kernel','rootfs','key','public','output']:p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--template',type=Path,default=ROOT/'integrations/swupdate/sw-description-ab-sign-rollback')
    p.add_argument('--password-file',type=Path)
    p.add_argument('--exclude-root',type=Path,action='append',default=[])
    p.add_argument('--version',required=True);p.add_argument('--epoch',type=int,required=True)
    a=p.parse_args()
    try:r=package(a.kernel,a.rootfs,a.template,a.key,a.public,a.password_file,a.output.absolute(),a.version,a.epoch,a.exclude_root)
    except (ValueError,OSError,subprocess.CalledProcessError) as e:p.exit(1,'SWU BUILD BLOCKED: '+str(e)+'\n')
    print(json.dumps(dict(sha256=r['package_sha256'],bytes=r['package_bytes'],device_acceptance='not_run')))


if __name__=='__main__':main()
