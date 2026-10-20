"""Exercise the real C layout reader/writer against ambiguous persistent inputs."""
import ctypes
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'apps/lv_port_linux/src/ui/desktop_widget_layout.c'
BASE = b'{"schema":1,"revision":0,"slots":["assistant","hdmi","environment","iot","connection"]}'


class Layout(ctypes.Structure):
    _fields_ = [('revision', ctypes.c_uint32), ('slots', ctypes.c_int * 5)]


class WidgetInputTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='mimi-layout-input-')
        cls.library = str(Path(cls.build.name) / 'layout.so')
        subprocess.run(['cc', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                        str(SOURCE), '-ljson-c', '-o', cls.library], check=True)
        cls.lib = ctypes.CDLL(cls.library)
        for name in ('load', 'save'):
            getattr(cls.lib, 'mimi_widget_layout_' + name).argtypes = [ctypes.c_char_p, ctypes.POINTER(Layout)]
        cls.lib.mimi_widget_layout_default.argtypes = [ctypes.POINTER(Layout)]

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def reject_and_preserve(self, data):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'layout.json'
            path.write_bytes(data)
            layout = Layout()
            self.lib.mimi_widget_layout_default(ctypes.byref(layout))
            before = bytes(layout)
            self.assertLess(self.lib.mimi_widget_layout_load(os.fsencode(path), ctypes.byref(layout)), 0)
            self.assertEqual(bytes(layout), before)
            self.assertLess(self.lib.mimi_widget_layout_save(os.fsencode(path), ctypes.byref(layout)), 0)
            self.assertEqual(bytes(layout), before)
            self.assertEqual(path.read_bytes(), data)

    def test_duplicate_future_schema(self):
        self.reject_and_preserve(BASE.replace(b'"schema":1', b'"schema":2,"schema":1'))

    def test_duplicate_revision(self):
        self.reject_and_preserve(BASE.replace(b'"revision":0', b'"revision":19,"revision":0'))

    def test_duplicate_slots(self):
        self.reject_and_preserve(BASE.replace(b'"slots":', b'"slots":[],"slots":'))

    def test_escaped_duplicate_key(self):
        self.reject_and_preserve(BASE.replace(b'"schema":1', b'"sc\\u0068ema":2,"schema":1'))

    def test_escaped_nul_in_key(self):
        self.reject_and_preserve(BASE.replace(b'"schema"', b'"schema\\u0000hidden"'))

    def test_nul_and_hidden_tail(self):
        for data in (BASE + b'\0future-data', BASE + b'\0', b'\0' + BASE):
            with self.subTest(data=data):
                self.reject_and_preserve(data)

    def test_malformed_nested_and_escaped_content(self):
        for data in (BASE.replace(b'"schema":1', b'"schema":{"hidden":1}'),
                     BASE.replace(b'"assistant"', b'"assistant\\\":1"'),
                     BASE.replace(b'"assistant"', b'"assistant\\u0000"'),
                     BASE + b'{}', BASE[:-1]):
            with self.subTest(data=data):
                self.reject_and_preserve(data)

    def test_existing_schema_escaped_keys_whitespace_roundtrip(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'layout.json'
            path.write_bytes(b'\n\t' + BASE.replace(b'"schema"', b'"sc\\u0068ema"') + b'\r\n ')
            layout = Layout()
            self.assertEqual(self.lib.mimi_widget_layout_load(os.fsencode(path), ctypes.byref(layout)), 0)
            self.assertEqual(list(layout.slots), [1, 2, 3, 4, 5])
            self.assertEqual(self.lib.mimi_widget_layout_save(os.fsencode(path), ctypes.byref(layout)), 0)
            self.assertEqual(layout.revision, 1)
            self.assertEqual(self.lib.mimi_widget_layout_load(os.fsencode(path), ctypes.byref(layout)), 0)
            self.assertEqual(layout.revision, 1)

    def test_fifo_data_rejected_without_blocking(self):
        self.special_file('.json', 'load')

    def test_fifo_lock_rejected_without_blocking(self):
        self.special_file('.json.lock', 'save')

    def special_file(self, suffix, operation):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'layout.json'
            fifo = Path(tmp) / ('layout' + suffix)
            os.mkfifo(fifo, 0o600)
            code = '''import ctypes,sys
class L(ctypes.Structure):
    _fields_=[('revision',ctypes.c_uint32),('slots',ctypes.c_int*5)]
lib=ctypes.CDLL(sys.argv[1]);x=L();lib.mimi_widget_layout_default(ctypes.byref(x))
before=bytes(x)
r=getattr(lib,'mimi_widget_layout_'+sys.argv[3])(sys.argv[2].encode(),ctypes.byref(x))
assert r < 0 and bytes(x)==before,(r,bytes(x))
'''
            subprocess.run([sys.executable, '-c', code, self.library, str(path), operation], check=True, timeout=2)
            self.assertTrue(fifo.is_fifo())
            if operation == 'save':
                self.assertFalse(path.exists())


if __name__ == '__main__':
    unittest.main()
