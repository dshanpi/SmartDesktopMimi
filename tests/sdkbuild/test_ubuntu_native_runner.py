"""Actual Ubuntu native ELF/SDK process isolation. No installed app/production data."""
import hashlib
import json
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest

ROOT=Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.geteuid()==0,'requires isolated root-created native executables')
class NativeRunnerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='mimi-native-test-',dir='/var/lib')
        cls.base=Path(cls.tmp.name);cls.base.chmod(0o755)
        cls.runner=cls.base/'runner';cls.program=cls.base/'probe'
        cls.private=cls.base/'private';cls.private.write_text('private fixture only');cls.private.chmod(0o600)
        cls.writable=cls.base/'writable';cls.writable.mkdir(mode=0o777);cls.writable.chmod(0o777)
        cls.write_path=cls.writable/'forbidden'
        subprocess.run(['cc','-std=gnu11','-O2','-Wall','-Wextra','-Werror',str(ROOT/'services/mimi-ubuntu-apps/native_runner.c'),'-o',str(cls.runner)],check=True)
        sdk=ROOT/'platform/app-sdk'
        subprocess.run(['cc','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-I'+str(sdk/'include'),'-DPRIVATE_PATH="'+str(cls.private)+'"','-DWRITE_PATH="'+str(cls.write_path)+'"',str(ROOT/'tests/sdkbuild/fixtures/ubuntu_native_probe.c'),str(sdk/'src/app.c'),'-ljson-c','-o',str(cls.program)],check=True)
        print(json.dumps({'runner_sha256':hashlib.sha256(cls.runner.read_bytes()).hexdigest(),'actual_probe_sha256':hashlib.sha256(cls.program.read_bytes()).hexdigest(),'architecture':os.uname().machine}),flush=True)

    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()

    def launch(self,action,path=None,sock=None,extra=(),open_mode=os.O_RDONLY):
        fd=os.open(str(path or self.program),open_mode)
        try:
            return subprocess.Popen([str(self.runner),'--executable-fd',str(fd),'--action',action,'--socket',str(sock or self.base/'absent.sock')],pass_fds=(fd,)+extra,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=dict(os.environ,MIMI_LEAK_ME='fixture-not-secret',LD_LIBRARY_PATH='/nonexistent/fixture'))
        finally:os.close(fd)

    def reply(self,action,**kw):
        p=self.launch(action,**kw)
        try:out,err=p.communicate(timeout=6)
        except BaseException:p.kill();p.communicate();raise
        self.assertEqual(p.returncode,0,(out,err));return json.loads(out)

    def test_actual_sdk_and_environment(self):self.assertTrue(self.reply('normal')['ok'])

    def test_original_program_reexec_refused(self):self.assertTrue(self.reply('self')['ok'])
    def test_nonnull_64bit_limit_pointer_refused(self):self.assertTrue(self.reply('prlimit-pointer')['ok'])

    def test_write_capable_executable_descriptor_refused(self):
        p=self.launch('normal',open_mode=os.O_RDWR);p.communicate(timeout=3);self.assertEqual(p.returncode,125)

    def test_parent_exit_terminates_native_child(self):
        readfd,writefd=os.pipe();worker=os.fork()
        if worker==0:
            os.close(readfd)
            try:
                p=self.launch('hold')
                if not json.loads(p.stdout.readline())['ok']:os._exit(99)
                native=Path('/proc/'+str(p.pid)+'/task/'+str(p.pid)+'/children').read_text().strip()
                os.write(writefd,(str(p.pid)+' '+native+'\n').encode());os.close(writefd)
                signal.pause()
            finally:os._exit(99)
        os.close(writefd)
        child=None
        try:
            with os.fdopen(readfd) as pipe:children=[int(x) for x in pipe.readline().split()]
            self.assertEqual(len(children),2)
            child=children[-1]
            os.kill(worker,signal.SIGKILL);os.waitpid(worker,0);worker=None
            deadline=time.monotonic()+3
            remaining=set(children)
            while time.monotonic()<deadline and remaining:
                for pid in list(remaining):
                    try:state=(Path('/proc')/str(pid)/'stat').read_text().split(') ',1)[1].split()[0]
                    except (FileNotFoundError,ProcessLookupError):state='gone'
                    if state in ('Z','gone'):remaining.remove(pid)
                if remaining:time.sleep(0.02)
            self.assertFalse(remaining,'monitor or native child survived caller exit')
            child=None
        finally:
            if worker is not None:
                os.kill(worker,signal.SIGKILL);os.waitpid(worker,0)
            if child:
                try:os.kill(child,signal.SIGKILL)
                except ProcessLookupError:pass

    def test_read_private_file_refused(self):self.assertTrue(self.reply('private')['ok'])

    def test_filesystem_write_refused(self):
        self.assertTrue(self.reply('write')['ok']);self.assertFalse(self.write_path.exists())

    def test_internet_refused(self):self.assertTrue(self.reply('internet')['ok'])
    def test_socketpair_refused(self):self.assertTrue(self.reply('socketpair')['ok'])
    def test_fork_refused(self):self.assertTrue(self.reply('fork')['ok'])
    def test_other_executable_refused(self):self.assertTrue(self.reply('exec')['ok'])
    def test_absolute_path_cannot_override_original_fd(self):self.assertTrue(self.reply('exec-absolute')['ok'])
    def test_other_executable_fd_refused(self):self.assertTrue(self.reply('exec-other-fd')['ok'])
    def test_original_fd_close_refused(self):self.assertTrue(self.reply('close-code')['ok'])
    def test_original_fd_replacement_refused(self):self.assertTrue(self.reply('replace-code')['ok'])
    def test_original_fd_cloexec_refused(self):self.assertTrue(self.reply('cloexec-code')['ok'])
    def test_device_ioctl_refused(self):self.assertTrue(self.reply('ioctl')['ok'])
    def test_dumpable_change_refused(self):self.assertTrue(self.reply('prctl')['ok'])

    def test_surplus_fd_not_inherited(self):
        fd=os.open(str(self.private),os.O_RDONLY)
        try:
            os.dup2(fd,17)
            try:self.assertTrue(self.reply('leak',extra=(17,))['ok'])
            finally:os.close(17)
        finally:os.close(fd)

    def test_kernel_identity_and_limits(self):
        p=self.launch('hold')
        try:
            self.assertTrue(json.loads(p.stdout.readline())['ok'])
            native=Path('/proc/'+str(p.pid)+'/task/'+str(p.pid)+'/children').read_text().strip()
            self.assertTrue(native.isdigit())
            lines=Path('/proc/'+native+'/status').read_text().splitlines()
            fields={x.split(':',1)[0]:x.split(':',1)[1].strip() for x in lines if ':' in x}
            self.assertEqual(fields['Uid'].split(),['65534']*4)
            self.assertEqual(fields['Gid'].split(),['65534']*4)
            self.assertEqual(fields['Groups'],'')
            self.assertEqual(fields['NoNewPrivs'],'1');self.assertEqual(fields['Seccomp'],'2')
            self.assertEqual(int(fields['CapEff'],16),0)
            self.assertEqual(fields['PPid'],str(p.pid))
            limits=Path('/proc/'+native+'/limits').read_text()
            self.assertRegex(limits,r'Max open files\s+32\s+32')
            self.assertRegex(limits,r'Max file size\s+0\s+0')
            self.assertRegex(limits,r'Max cpu time\s+2\s+2')
        finally:p.kill();p.communicate()

    def test_cpu_bound_process_is_killed(self):
        p=self.launch('cpu')
        try:p.communicate(timeout=6)
        except BaseException:p.kill();p.communicate();raise
        # A loaded host can reach the wall deadline before using 2 CPU seconds.
        self.assertIn(p.returncode,(128+signal.SIGKILL,124))

    def test_wall_deadline_terminates_sleeping_native(self):
        p=self.launch('hold');start=time.monotonic()
        out,_=p.communicate(timeout=7)
        self.assertTrue(json.loads(out)['ok']);self.assertEqual(p.returncode,124)
        self.assertLess(time.monotonic()-start,6.5)

    def test_transfer_descriptor_cannot_be_closed(self):self.assertTrue(self.reply('close-transfer')['ok'])
    def test_transfer_descriptor_cannot_be_replaced(self):self.assertTrue(self.reply('replace-transfer')['ok'])
    def test_transfer_descriptor_cannot_reconnect(self):self.assertTrue(self.reply('connect-transfer')['ok'])
    def test_filter_cannot_be_replaced(self):self.assertTrue(self.reply('seccomp')['ok'])
    def test_rights_transfer_to_other_fd_refused(self):self.assertTrue(self.reply('sendmsg')['ok'])

    def test_privileged_output_file_not_passed_to_child(self):
        fd=os.open(str(self.program),os.O_RDONLY)
        try:
            with open(self.base/'stdout-file','wb') as output:
                p=subprocess.run([str(self.runner),'--executable-fd',str(fd),'--action','normal','--socket',str(self.base/'absent.sock')],pass_fds=(fd,),stdout=output,timeout=3)
            self.assertEqual(p.returncode,125)
            self.assertEqual((self.base/'stdout-file').read_bytes(),b'')
        finally:os.close(fd)

    def test_wrong_architecture_refused_before_exec(self):
        target=self.base/'wrong-machine';b=bytearray(self.program.read_bytes());struct.pack_into('<H',b,18,183);target.write_bytes(b);target.chmod(0o755)
        p=self.launch('normal',path=target);out,_=p.communicate(timeout=3);self.assertEqual(p.returncode,125);self.assertEqual(out,'')

    def test_writable_native_file_refused(self):
        self.program.chmod(0o777)
        try:
            p=self.launch('normal');p.communicate(timeout=3);self.assertEqual(p.returncode,125)
        finally:self.program.chmod(0o755)

    def test_nonroot_runner_entry_refused(self):
        p=subprocess.run(['runuser','-u','nobody','--',str(self.runner)],capture_output=True,timeout=3);self.assertEqual(p.returncode,125)

    def test_real_sdk_unix_transport_and_peer_credentials(self):
        path=self.base/'broker-test.sock';server=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);server.bind(str(path));path.chmod(0o666);server.listen(1);server.settimeout(4)
        seen={}
        def handle():
            try:
                with server.accept()[0] as c:
                    c.settimeout(3);seen['peer']=struct.unpack('3i',c.getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,12));seen['request']=json.loads(c.makefile('rb').readline(4096));c.sendall(b'{"ok":true,"result":{"transport_only":true}}\n')
            except Exception as exc:seen['error']=str(exc)
        t=threading.Thread(target=handle);t.start();p=self.launch('transport',sock=path)
        try:
            out,err=p.communicate(timeout=5);t.join(5);self.assertEqual(p.returncode,0,(out,err));self.assertTrue(json.loads(out)['ok']);self.assertNotIn('error',seen);self.assertNotEqual(seen['peer'][0],p.pid);self.assertEqual(seen['peer'][1:],(65534,65534));self.assertEqual(seen['request']['command'],'capability')
        finally:
            if p.poll() is None:p.kill();p.communicate()
            server.close();t.join(5);path.unlink(missing_ok=True)


if __name__=='__main__':unittest.main()
