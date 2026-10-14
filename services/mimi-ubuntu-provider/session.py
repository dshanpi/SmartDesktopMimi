#!/usr/bin/env python3
"""Run a local Ubuntu application session as its desktop user.

This is a platform lifecycle component, not an installer or signature verifier.
Trusted deployment must validate the selected release before invoking it.
It never modifies updater state, enrollment, keys, hardware or host settings.
"""
import argparse
import ctypes
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import socket
import stat
import struct
import subprocess
import time

stopping=False
libc=ctypes.CDLL(None,use_errno=True)
libc.prctl.argtypes=[ctypes.c_int,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_ulong]
libc.prctl.restype=ctypes.c_int

def child_guard(parent_pid):
    # Only called in the single-threaded launcher child before exec.
    if libc.prctl(1,signal.SIGKILL,0,0,0)!=0 or os.getppid()!=parent_pid:
        os._exit(126)

def stop_requested(sig,frame):
    global stopping
    stopping=True

def canonical(path):
    if any(ord(c)<32 for c in str(path)) or not path.is_absolute() or str(path.resolve(strict=True))!=str(path):
        raise ValueError('Path must be absolute, canonical and present')
    return path

def private_directory(path):
    canonical(path);s=path.lstat()
    if not stat.S_ISDIR(s.st_mode) or s.st_uid!=os.geteuid() or stat.S_IMODE(s.st_mode)!=0o700:
        raise ValueError('Expected an owned private directory')

def regular(path,private=False):
    s=path.lstat()
    if not stat.S_ISREG(s.st_mode) or s.st_nlink!=1:
        raise ValueError('Expected a regular, unlinked file')
    if private and (s.st_uid!=os.geteuid() or stat.S_IMODE(s.st_mode)!=0o600):
        raise ValueError('Expected an owned private file')

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1<<20),b''):h.update(b)
    return h.hexdigest()

def read_json(path,private=False):
    regular(path,private)
    if path.stat().st_size>1<<20:raise ValueError('JSON input exceeds limit')
    def unique(items):
        d={}
        for k,v in items:
            if k in d:raise ValueError('Duplicate JSON key')
            d[k]=v
        return d
    return json.loads(path.read_text(),object_pairs_hook=unique)

def write_private(path,data):
    temporary=path.with_name(path.name+'.tmp')
    fd=os.open(temporary,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    try:
        with os.fdopen(fd,'w') as f:f.write(data)
        os.replace(temporary,path)
    finally:
        if temporary.exists():temporary.unlink()

def process_identity(pid):
    # A process name may contain spaces/parentheses; fields after the last ')' start at3.
    fields=Path('/proc',str(pid),'stat').read_text().rsplit(')',1)[1].split()
    return {'pid':pid,'start_ticks':int(fields[19]),'exe':os.readlink('/proc/'+str(pid)+'/exe')}

def process_matches(record):
    return type(record.get('pid')) is int and record['pid']>1 and process_identity(record['pid'])==record

def query(path,topic):
    with socket.socket(socket.AF_UNIX) as s:
        s.settimeout(1);s.connect(str(path))
        q=dict(schemaVersion=2,messageType='request',topic=topic,requestId='session-health',payload={})
        body=json.dumps(q).encode();s.sendall(struct.pack('>I',len(body))+body)
        def exact(n):
            b=b''
            while len(b)<n:
                chunk=s.recv(n-len(b))
                if not chunk:raise ValueError('Provider disconnected')
                b+=chunk
            return b
        size=struct.unpack('>I',exact(4))[0]
        if not 0<size<=65536:raise ValueError('Invalid provider frame')
        r=json.loads(exact(size))
        if r.get('schemaVersion')!=2 or r.get('messageType')!='response' or r.get('topic')!=topic or r.get('requestId')!='session-health' or 'error' in r:
            raise ValueError('Provider query rejected')
        return r['payload']

def health(runtime,release,starting=False):
    private_directory(runtime);canonical(release)
    state=read_json(runtime/'session.json',True)
    if state.get('release')!=str(release) or (not starting and state.get('ready') is not True):
        raise ValueError('Session is not ready for the selected release')
    for name in ('supervisor','desktop','provider'):
        if not process_matches(state[name]):raise ValueError('Session process identity changed')
    if state['desktop']['exe']!=str(release/'bin/lvglsim'):
        raise ValueError('Desktop executable does not match release')
    h=read_json(runtime/'ui-health.json',True);now=int(time.monotonic()*1000)
    if type(h.get('pid')) is not int or h['pid']!=state['desktop']['pid']:
        raise ValueError('Health belongs to another desktop')
    for field,maximum in [('monotonic_ms',4000),('backend_reply_ms',5000)]:
        if type(h.get(field)) is not int or h[field]<=0 or not 0<=now-h[field]<=maximum:
            raise ValueError('Desktop event loop or backend status is stale')
    if h.get('activity_ready') is not True or type(h.get('busy')) is not bool:
        raise ValueError('Activity status is incomplete')
    if query(runtime/'ipc.sock.v2','system.ping').get('alive') is not True:
        raise ValueError('Provider did not answer ping')
    d=query(runtime/'ipc.sock.v2','platform.describe')
    if d.get('platformId')!='ubuntu-host' or d.get('architecture')!='x86_64' or d.get('abiVersion')!=2 or d.get('capabilities')!=0 or d.get('providerVersion')!='0.2.0':
        raise ValueError('Unexpected Ubuntu foundation provider')
    return h

def stop_children(children):
    for child in children:
        if child.poll() is None:child.terminate()
    deadline=time.monotonic()+3
    for child in children:
        try:child.wait(timeout=max(.01,deadline-time.monotonic()))
        except subprocess.TimeoutExpired:child.kill();child.wait(timeout=3)

def run(release,provider,runtime,settings,screen):
    if os.geteuid()==0:raise ValueError('Desktop sessions must run as the desktop user, not root')
    os.umask(0o077);canonical(release);canonical(provider);regular(provider);private_directory(runtime);private_directory(settings.parent)
    if len(str(runtime/'ipc.sock.v2').encode())>=108:raise ValueError('Runtime socket path too long')
    if runtime==settings.parent or runtime in settings.parents or release in settings.parents or release==runtime or release in runtime.parents or runtime in release.parents or release in provider.parents:
        raise ValueError('Release, provider, runtime and persistent settings must remain separate')
    if settings.exists() or settings.is_symlink():regular(settings,True)
    receipt=read_json(release/'resources/application-build.json')
    if receipt.get('kind')!='application-build' or receipt.get('schema_version')!=1 or (receipt.get('platform_id'),receipt.get('architecture'),receipt.get('target_os'))!=('ubuntu-host','x86_64','ubuntu') or receipt.get('source',{}).get('dirty') is not False:
        raise ValueError('Expected a clean Ubuntu SDK application build')
    desktop=release/'bin/lvglsim';regular(desktop)
    if digest(desktop)!=receipt.get('desktop_sha256'):raise ValueError('Desktop differs from build receipt')
    if not re.fullmatch('[1-9][0-9]{2,3}x[1-9][0-9]{2,3}',screen) or screen not in receipt.get('build',{}).get('display_profiles',[]):
        raise ValueError('Screen is not in the selected application build')
    lock=os.open(runtime/'session.lock',os.O_CREAT|os.O_RDWR|os.O_NOFOLLOW|os.O_CLOEXEC,0o600)
    children=[];owned=[]
    try:
        st=os.fstat(lock)
        if not stat.S_ISREG(st.st_mode) or st.st_nlink!=1 or st.st_uid!=os.geteuid() or stat.S_IMODE(st.st_mode)!=0o600:raise ValueError('Invalid session lock')
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        # Only known regular private transient files are reclaimed after locking.
        for name in ('session.json','session.json.tmp','ui-health.json','ui-health.tmp','desktop.conf','desktop.conf.tmp'):
            p=runtime/name
            if p.exists() or p.is_symlink():regular(p,True);p.unlink()
            owned.append(p)
        cfg=runtime/'desktop.conf';write_private(cfg,'ipc.socket_path='+str(runtime/'ipc.sock')+'\n')
        parent_pid=os.getpid()
        backend=subprocess.Popen([str(provider),'--socket',str(runtime/'ipc.sock')],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,preexec_fn=lambda:child_guard(parent_pid))
        children.append(backend);deadline=time.monotonic()+5
        while time.monotonic()<deadline and not stopping:
            if backend.poll() is not None:raise ValueError('Provider exited during startup')
            try:
                if query(runtime/'ipc.sock.v2','system.ping').get('alive') is True:break
            except (OSError,ValueError):pass
            time.sleep(.1)
        else:raise ValueError('Provider startup timed out or cancelled')
        env=dict(os.environ,MIMI_RELEASE_DIR=str(release),MIMI_RUNTIME_DIR=str(runtime),LV_PORT_LINUX_CONFIG=str(cfg),LV_SETTINGS_FILE=str(settings))
        width,height=screen.split('x')
        ui=subprocess.Popen([str(desktop),'-b','SDL','-W',width,'-H',height],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,preexec_fn=lambda:child_guard(parent_pid))
        children.append(ui)
        state=dict(release=str(release),ready=False,supervisor=process_identity(os.getpid()),desktop=process_identity(ui.pid),provider=process_identity(backend.pid))
        # Popen returns after exec on POSIX, so these identities are the actual executables.
        write_private(runtime/'session.json',json.dumps(state))
        last=0;consecutive=0;deadline=time.monotonic()+20
        while time.monotonic()<deadline and not stopping:
            if any(p.poll() is not None for p in children):raise ValueError('Session child exited before readiness')
            try:
                h=health(runtime,release,True)
                if h['monotonic_ms']>last:last=h['monotonic_ms'];consecutive+=1
                if consecutive>=3:break
            except (OSError,ValueError,KeyError):consecutive=0
            time.sleep(.25)
        else:raise ValueError('Desktop readiness timed out or cancelled')
        state['ready']=True;write_private(runtime/'session.json',json.dumps(state));print('Ubuntu desktop session ready',flush=True)
        while not stopping:
            if backend.poll() is not None:raise ValueError('Provider exited')
            if ui.poll() is not None:
                if ui.returncode==0:return
                raise ValueError('Desktop exited with failure')
            try:health(runtime,release)
            except (OSError,ValueError,KeyError):
                if ui.poll()==0 and backend.poll() is None:return
                raise
            time.sleep(.25)
    finally:
        stop_children(children)
        for p in owned:
            if p.exists() or p.is_symlink():
                try:regular(p,True);p.unlink()
                except (OSError,ValueError):pass
        os.close(lock)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('operation',choices=['run','health']);p.add_argument('--release',type=Path,required=True);p.add_argument('--runtime',type=Path,required=True);p.add_argument('--provider',type=Path);p.add_argument('--settings',type=Path);p.add_argument('--screen',default='1024x768');a=p.parse_args()
    signal.signal(signal.SIGTERM,stop_requested);signal.signal(signal.SIGINT,stop_requested)
    try:
        if a.operation=='health':print(json.dumps(health(a.runtime,a.release)));return
        if not a.provider or not a.settings:raise ValueError('Provider and persistent settings are required')
        run(a.release,a.provider,a.runtime,a.settings,a.screen)
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as e:p.exit(1,'SESSION FAILED: '+str(e)+'\n')

if __name__=='__main__':main()
