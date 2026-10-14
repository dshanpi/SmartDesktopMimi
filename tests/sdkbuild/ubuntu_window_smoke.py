#!/usr/bin/env python3
"""Exercise the compiled desktop in an isolated X server with an IPC sink or an explicitly selected real provider.
This sink provides no hardware/service replies and cannot qualify a Provider.
"""
import argparse
import ctypes as c
import json
import os
from pathlib import Path
import socket
import struct
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
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--provider',type=Path);a=p.parse_args()
    out=a.output.absolute();out.mkdir(parents=True,exist_ok=False);out.chmod(0o700)
    sock_path=out/'ipc.sock'
    if len(str(sock_path).encode())>=104: raise ValueError('Test socket path too long')
    cfg=out/'desktop.conf';cfg.write_text('ipc.socket_path='+str(sock_path)+'\n')
    stop=threading.Event()
    provider=None;provider_log=None;sock=None;thread=None
    if not a.provider:
        sock=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);sock.bind(str(sock_path));sock.listen(4);sock.settimeout(.5)
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
    else:
        provider_log=(out/'provider.log').open('w')
        provider=subprocess.Popen([str(a.provider.resolve()),'--socket',str(sock_path)],stdout=provider_log,stderr=subprocess.STDOUT)
        for _ in range(100):
            if sock_path.exists() and 'Ubuntu session provider ready' in (out/'provider.log').read_text():break
            if provider.poll() is not None:provider_log.close();raise RuntimeError('Provider startup failed')
            time.sleep(.02)
        else:
            provider.terminate();provider.wait(timeout=5);provider_log.close();raise RuntimeError('Provider startup timeout')
    read_fd,write_fd=os.pipe();xlog=(out/'xvfb.log').open('w')
    xvfb=subprocess.Popen(['Xvfb','-displayfd',str(write_fd),'-screen','0','1920x1080x24','-nolisten','tcp'],pass_fds=(write_fd,),stdout=xlog,stderr=subprocess.STDOUT)
    os.close(write_fd)
    with os.fdopen(read_fd) as f: display=':'+f.readline().strip()
    processes=[];result={'scope':('native desktop with real Ubuntu session provider; unsupported board/cloud controls remain unavailable' if provider else 'native desktop window with IPC sink; no real platform services'),'checks':[]}
    try:
        assert display!=':' and xvfb.poll() is None
        if provider:
            with socket.socket(socket.AF_UNIX) as query:
                query.settimeout(3);query.connect(str(sock_path)+'.v2')
                for topic in ('system.ping','platform.describe'):
                    request=dict(schemaVersion=2,messageType='request',topic=topic,requestId='window-check',payload={})
                    body=json.dumps(request).encode();query.sendall(struct.pack('>I',len(body))+body)
                    def exact(n):
                        b=b''
                        while len(b)<n:
                            part=query.recv(n-len(b));assert part;b+=part
                        return b
                    size=struct.unpack('>I',exact(4))[0];assert 0<size<=65536
                    reply=json.loads(exact(size));assert reply['schemaVersion']==2 and reply['messageType']=='response'
                    assert reply['requestId']=='window-check' and reply['topic']==topic and 'error' not in reply
                    if topic=='system.ping':assert reply['payload']['alive'] is True
                    else:assert reply['payload']['platformId']=='ubuntu-host' and reply['payload']['capabilities']==0
                    result.setdefault('provider_v2',{})[topic]=reply['payload']
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
            if provider and width==1024:
                # Exercise real SDL pointer routing to the existing pairing page.
                def move(x,y):
                    subprocess.run(['xdotool','mousemove','--window',str(window),str(x),str(y)],env=env,check=True)
                move(850,635);subprocess.run(['xdotool','mousedown','1'],env=env,check=True);time.sleep(.1)
                for px in range(825,249,-25):move(px,635);time.sleep(.025)
                subprocess.run(['xdotool','mouseup','1'],env=env,check=True);time.sleep(.5)
                move(400,635);subprocess.run(['xdotool','mousedown','1'],env=env,check=True);time.sleep(.1)
                subprocess.run(['xdotool','mouseup','1'],env=env,check=True);time.sleep(1)
                subprocess.run(['import','-window',str(window),str(out/'pairing-provider.png')],env=env,check=True,timeout=10)
                result['pairing_screenshot']='pairing-provider.png (requires visual review)'
                assert provider.poll() is None
            close_window(display,window);assert process.wait(timeout=10)==0;log.close()
            result['checks'].append({'resolution':name,'window':'passed','close':'passed'})
        result['status']='passed'
    except Exception as error:
        result.update(status='failed',error=repr(error));raise
    finally:
        for process in processes:
            if process.poll() is None:process.terminate();process.wait(timeout=5)
        stop.set()
        if sock:sock.close()
        if thread:thread.join(timeout=2)
        if provider:
            provider.terminate();provider.wait(timeout=5);provider_log.close()
            result['provider_shutdown_exit']=provider.returncode
            result['provider_socket_removed']=not sock_path.exists()
            result['provider_v2_socket_removed']=not Path(str(sock_path)+'.v2').exists()
        xvfb.terminate();xvfb.wait(timeout=5);xlog.close()
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        sock_path.unlink(missing_ok=True)
        if provider and (result['provider_shutdown_exit'] != 0 or not result['provider_socket_removed'] or not result['provider_v2_socket_removed']):
            result.update(status='failed',error='Provider did not shut down cleanly')
            (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
            raise RuntimeError(result['error'])


if __name__=='__main__':main()
