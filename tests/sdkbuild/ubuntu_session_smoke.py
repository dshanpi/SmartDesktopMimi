#!/usr/bin/env python3
"""Actual Ubuntu SDL/provider session acceptance in an isolated X server."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import time
from ubuntu_window_smoke import close_window

ROOT=Path(__file__).resolve().parents[2]
LAUNCHER=ROOT/'services/mimi-ubuntu-provider/session.py'

def live(record):
    try:
        fields=Path('/proc',str(record['pid']),'stat').read_text().rsplit(')',1)[1].split()
        return fields[0]!='Z' and int(fields[19])==record['start_ticks']
    except FileNotFoundError:return False

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--release',type=Path,required=True);p.add_argument('--provider',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.absolute();out.mkdir(mode=0o700,parents=True,exist_ok=False)
    runtime=out/'run';runtime.mkdir(mode=0o700);persistent=out/'data';persistent.mkdir(mode=0o700)
    settings=persistent/'settings.json';sentinel=persistent/'user-data';sentinel.write_text('retained user data\n')
    result=dict(scope='Real SDK desktop/Provider processes in Xvfb; not signed activation or physical panel',release=str(a.release),desktop_sha256=hashlib.sha256((a.release/'bin/lvglsim').read_bytes()).hexdigest(),provider_sha256=hashlib.sha256(a.provider.read_bytes()).hexdigest(),checks=[])
    xf=(out/'xvfb.log').open('w');rd,wr=os.pipe();xvfb=None;session=None;records=[];logs=[]
    try:
        xvfb=subprocess.Popen(['Xvfb','-displayfd',str(wr),'-screen','0','1024x768x24','-nolisten','tcp'],pass_fds=(wr,),stdout=xf,stderr=subprocess.STDOUT)
        os.close(wr);wr=-1
        if not select.select([rd],[],[],5)[0]:raise RuntimeError('Xvfb startup timeout')
        display=':'+os.read(rd,32).decode().strip();os.close(rd);rd=-1
        assert display!=':' and xvfb.poll() is None
        env=dict(os.environ,DISPLAY=display,SDL_AUDIODRIVER='dummy')
        command=[sys.executable,str(LAUNCHER),'run','--release',str(a.release),'--provider',str(a.provider),'--runtime',str(runtime),'--settings',str(settings),'--screen','1024x768']
        def start(label):
            nonlocal session
            log=(out/(label+'.log')).open('w');logs.append(log)
            session=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
            for _ in range(180):
                if session.poll() is not None:raise RuntimeError(label+' exited before readiness: '+(out/(label+'.log')).read_text())
                try:
                    state=json.loads((runtime/'session.json').read_text())
                    if state.get('ready') is True and state['supervisor']['pid']==session.pid:
                        records.append(state);return state
                except (FileNotFoundError,json.JSONDecodeError):pass
                time.sleep(.2)
            raise RuntimeError(label+' readiness timeout')
        def check_health():
            q=subprocess.run([sys.executable,str(LAUNCHER),'health','--release',str(a.release),'--runtime',str(runtime)],env=env,capture_output=True,text=True,timeout=5)
            assert q.returncode==0,q.stderr
            return json.loads(q.stdout)
        def stopped(state):
            assert not live(state['desktop']) and not live(state['provider'])
            assert not any((runtime/name).exists() for name in ('session.json','ui-health.json','ui-health.tmp','desktop.conf','ipc.sock','ipc.sock.v2'))
            assert sentinel.read_text()=='retained user data\n'
        state=start('start');h1=check_health();time.sleep(1.1);h2=check_health();assert h2['monotonic_ms']>h1['monotonic_ms']
        duplicate=subprocess.run(command,env=env,capture_output=True,text=True,timeout=5);assert duplicate.returncode!=0;assert live(state['desktop'])
        window=None
        for _ in range(100):
            q=subprocess.run(['xdotool','search','--onlyvisible','--pid',str(state['desktop']['pid'])],env=env,capture_output=True,text=True)
            if q.returncode==0 and q.stdout.strip():window=int(q.stdout.splitlines()[0]);break
            time.sleep(.1)
        if not window:
            q=subprocess.run(['xwininfo','-root','-tree'],env=env,capture_output=True,text=True,timeout=5);(out/'window-failure.log').write_text(q.stdout);raise RuntimeError('Healthy session did not expose a visible SDL window')
        subprocess.run(['import','-window',str(window),str(out/'session-home.png')],env=env,check=True,timeout=10)
        before=settings.read_bytes();close_window(display,window);assert session.wait(timeout=8)==0;stopped(state)
        result['checks'].append(dict(name='ready_progress_duplicate_and_window_close',status='passed',health_first=h1,health_second=h2))
        state=start('restart');assert settings.read_bytes()==before;session.terminate();assert session.wait(timeout=8)==0;stopped(state)
        result['checks'].append(dict(name='restart_keeps_settings_and_SIGTERM_cleans_children',status='passed',settings_sha256=hashlib.sha256(before).hexdigest()))
        state=start('provider-failure');os.kill(state['provider']['pid'],signal.SIGTERM);assert session.wait(timeout=8)!=0;stopped(state)
        result['checks'].append(dict(name='provider_exit_fails_session_and_stops_UI',status='passed'))
        state=start('ui-stall');os.kill(state['desktop']['pid'],signal.SIGSTOP);assert session.wait(timeout=15)!=0;stopped(state)
        result['checks'].append(dict(name='stalled_event_loop_rejected_and_terminated',status='passed'))
        state=start('supervisor-kill');session.kill();session.wait(timeout=3)
        for _ in range(50):
            if not live(state['desktop']) and not live(state['provider']):break
            time.sleep(.1)
        assert not live(state['desktop']) and not live(state['provider'])
        state=start('recover-after-kill');check_health();session.terminate();assert session.wait(timeout=8)==0;stopped(state)
        result['checks'].append(dict(name='supervisor_SIGKILL_terminates_children_and_restart_recovers',status='passed'))
        result['status']='passed'
    except Exception as e:result.update(status='failed',error=repr(e));raise
    finally:
        if session and session.poll() is None:
            session.terminate()
            try:session.wait(timeout=6)
            except subprocess.TimeoutExpired:session.kill();session.wait(timeout=3)
        for state in records:
            for key in ('desktop','provider'):
                if live(state[key]):os.kill(state[key]['pid'],signal.SIGKILL)
        if xvfb and xvfb.poll() is None:xvfb.terminate();xvfb.wait(timeout=5)
        if rd>=0:os.close(rd)
        if wr>=0:os.close(wr)
        for f in logs+[xf]:f.close()
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')

if __name__=='__main__':main()
