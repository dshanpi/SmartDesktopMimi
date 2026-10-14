#!/usr/bin/env python3
"""Exercise the compiled desktop in an isolated X server with an IPC sink.
This sink provides no hardware/service replies and cannot qualify a Provider.
"""
import argparse
import ctypes as c
import json
import os
from pathlib import Path
import socket
import subprocess
import threading
import time
from PIL import Image


def close_window(display, window):
    x = c.CDLL('libX11.so.6')
    x.XOpenDisplay.argtypes = [c.c_char_p]; x.XOpenDisplay.restype = c.c_void_p
    x.XInternAtom.argtypes = [c.c_void_p,c.c_char_p,c.c_int]; x.XInternAtom.restype = c.c_ulong
    class Message(c.Structure):
        _fields_ = [('type',c.c_int),('serial',c.c_ulong),('send_event',c.c_int),('display',c.c_void_p),('window',c.c_ulong),('message_type',c.c_ulong),('format',c.c_int),('data',c.c_long*5)]
    class Event(c.Union):
        _fields_ = [('message',Message),('pad',c.c_long*24)]
    x.XSendEvent.argtypes = [c.c_void_p,c.c_ulong,c.c_int,c.c_long,c.POINTER(Event)]
    x.XFlush.argtypes = [c.c_void_p]; x.XCloseDisplay.argtypes = [c.c_void_p]
    d = x.XOpenDisplay(display.encode()); assert d
    try:
        e=Event();e.message.type=33;e.message.display=d;e.message.window=window
        e.message.message_type=x.XInternAtom(d,b'WM_PROTOCOLS',0);e.message.format=32
        e.message.data[0]=x.XInternAtom(d,b'WM_DELETE_WINDOW',0)
        assert x.XSendEvent(d,window,0,0,c.byref(e)) != 0
        x.XFlush(d)
    finally: x.XCloseDisplay(d)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.absolute();out.mkdir(parents=True,exist_ok=False)
    sock_path=out/'ipc.sock'
    if len(str(sock_path).encode())>=104: raise ValueError('Test socket path too long')
    cfg=out/'desktop.conf';cfg.write_text('ipc.socket_path='+str(sock_path)+'\n')
    sock=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);sock.bind(str(sock_path));sock.listen(4);sock.settimeout(.5)
    stop=threading.Event()
    def sink():
        while not stop.is_set():
            try: conn,_=sock.accept()
            except socket.timeout: continue
            except OSError: return
            with conn:
                conn.settimeout(.5)
                while not stop.is_set():
                    try:
                        if not conn.recv(65536):break
                    except socket.timeout:pass
                    except OSError:break
    thread=threading.Thread(target=sink,daemon=True);thread.start()
    read_fd,write_fd=os.pipe();xlog=(out/'xvfb.log').open('w')
    xvfb=subprocess.Popen(['Xvfb','-displayfd',str(write_fd),'-screen','0','1920x1080x24','-nolisten','tcp'],pass_fds=(write_fd,),stdout=xlog,stderr=subprocess.STDOUT)
    os.close(write_fd)
    with os.fdopen(read_fd) as f: display=':'+f.readline().strip()
    processes=[];result={'scope':'native desktop window with IPC sink; no real platform services','checks':[]}
    try:
        assert display!=':' and xvfb.poll() is None
        for width,height in [(1024,768),(480,800),(800,480),(640,480)]:
            name=f'{width}x{height}';log=(out/(name+'.log')).open('w')
            env=dict(os.environ,DISPLAY=display,LV_PORT_LINUX_CONFIG=str(cfg),LV_SETTINGS_FILE=str(out/(name+'.settings')),SDL_AUDIODRIVER='dummy')
            process=subprocess.Popen([str(a.binary.resolve()),'-b','SDL','-W',str(width),'-H',str(height)],env=env,stdout=log,stderr=subprocess.STDOUT)
            processes.append(process)
            if width==640:
                assert process.wait(timeout=15)!=0
                log.close();assert 'Unsupported display 640x480' in (out/(name+'.log')).read_text()
                result['checks'].append({'resolution':name,'rejection':'passed'});continue
            window=None
            for _ in range(100):
                if process.poll() is not None: raise RuntimeError('Desktop exited early: '+name)
                q=subprocess.run(['xdotool','search','--onlyvisible','--pid',str(process.pid)],env=env,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
                if q.returncode==0 and q.stdout.strip():window=int(q.stdout.splitlines()[0]);break
                time.sleep(.1)
            assert window
            time.sleep(1)
            subprocess.run(['import','-window',str(window),str(out/(name+'.png'))],env=env,check=True,timeout=10)
            im=Image.open(out/(name+'.png'));assert im.size==(width,height)
            assert len(im.convert('RGB').getcolors(width*height))>30
            close_window(display,window);assert process.wait(timeout=10)==0;log.close()
            result['checks'].append({'resolution':name,'window':'passed','close':'passed'})
        result['status']='passed'
    except Exception as error:
        result.update(status='failed',error=repr(error));raise
    finally:
        for process in processes:
            if process.poll() is None:process.terminate();process.wait(timeout=5)
        stop.set();sock.close();thread.join(timeout=2)
        xvfb.terminate();xvfb.wait(timeout=5);xlog.close()
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        sock_path.unlink(missing_ok=True)


if __name__=='__main__':main()
