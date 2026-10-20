#!/usr/bin/env python3
"""Exercise actual SDL layout save/cancel/reload using isolated user data.

This is native GUI persistence evidence, not a signed update or full user-data
qualification. No sentinels stand in for media or extension consumers.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import time
from PIL import Image, ImageChops
from ubuntu_window_smoke import close_window

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--release', type=Path, required=True)
    p.add_argument('--provider', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--corrupt-inputs', action='store_true', help='Also reject ambiguous layout files through real save UI')
    a = p.parse_args()
    out = a.output.absolute()
    out.mkdir(parents=True, mode=0o700, exist_ok=False)
    runtime, data = out/'run', out/'data'
    runtime.mkdir(mode=0o700); data.mkdir(mode=0o700)
    settings = data/'settings.bin'
    layout = data/'settings.bin.widgets.json'
    result = {'scope': 'Actual current native SDL/Provider in isolated Xvfb; synthetic pointer input and dedicated data; not signed upgrade, real login, media or extension acceptance', 'checks': []}
    logs = []; session = None; xvfb = None; rd, wr = os.pipe()
    try:
        xf = (out/'xvfb.log').open('w'); logs.append(xf)
        xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(wr), '-screen', '0', '1024x768x24', '-nolisten', 'tcp'], pass_fds=(wr,), stdout=xf, stderr=subprocess.STDOUT)
        os.close(wr); wr = -1
        if not select.select([rd], [], [], 5)[0]: raise RuntimeError('Xvfb display timeout')
        display = ':' + os.read(rd, 32).decode().strip()
        os.close(rd); rd = -1
        assert display != ':' and xvfb.poll() is None
        env = dict(os.environ, DISPLAY=display, SDL_AUDIODRIVER='dummy')
        command = [sys.executable, str(ROOT/'services/mimi-ubuntu-provider/session.py'), 'run', '--release', str(a.release), '--provider', str(a.provider), '--runtime', str(runtime), '--settings', str(settings), '--screen', '1024x768']

        def start(label):
            nonlocal session
            log = (out/(label+'.log')).open('w'); logs.append(log)
            session = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
            for _ in range(150):
                if session.poll() is not None: raise RuntimeError(label+' exited before readiness')
                try:
                    state = json.loads((runtime/'session.json').read_text())
                    if state['ready'] and state['supervisor']['pid'] == session.pid:
                        q = subprocess.run(['xdotool', 'search', '--onlyvisible', '--pid', str(state['desktop']['pid'])], env=env, capture_output=True, text=True, timeout=3)
                        if q.returncode == 0 and q.stdout.strip(): return int(q.stdout.splitlines()[0])
                except (FileNotFoundError, json.JSONDecodeError): pass
                time.sleep(.2)
            raise RuntimeError(label+' window timeout')

        def click(window, x, y):
            subprocess.run(['xdotool', 'mousemove', '--window', str(window), str(x), str(y), 'mousedown', '1'], env=env, check=True, timeout=3)
            time.sleep(.12)
            subprocess.run(['xdotool', 'mouseup', '1'], env=env, check=True, timeout=3)
            time.sleep(.5)

        def capture(window, name):
            subprocess.run(['import', '-window', str(window), str(out/(name+'.png'))], env=env, check=True, timeout=10)

        def close(window):
            close_window(display, window)
            assert session.wait(timeout=8) == 0

        w = start('initial'); capture(w, 'initial')
        settings_before = settings.read_bytes()
        assert not layout.exists()
        click(w, 900, 84); capture(w, 'edit')
        # 1024x768 editor: remove environment slot, then commit through UI.
        click(w, 328, 434); capture(w, 'removed-draft')
        click(w, 932, 110); capture(w, 'saved')
        saved = layout.read_bytes(); record = json.loads(saved)
        assert record['slots'] == ['assistant', 'hdmi', 'empty', 'iot', 'connection'], record
        assert record['revision'] > 0
        close(w)
        w = start('reload'); capture(w, 'reloaded')
        assert layout.read_bytes() == saved and settings.read_bytes() == settings_before
        # Static empty-slot crop verifies the saved layout was consumed on restart.
        crop = (46, 407, 345, 575)
        before = Image.open(out/'initial.png').convert('RGB').crop(crop)
        committed = Image.open(out/'saved.png').convert('RGB').crop(crop)
        reloaded = Image.open(out/'reloaded.png').convert('RGB').crop(crop)
        assert ImageChops.difference(before, committed).getbbox() is not None
        assert ImageChops.difference(committed, reloaded).getbbox() is None
        result['checks'].append({'name': 'pointer_save_consumed_after_native_process_restart', 'status': 'passed', 'revision': record['revision'], 'layout_sha256': hashlib.sha256(saved).hexdigest()})
        click(w, 900, 84); capture(w, 'cancel-before')
        click(w, 642, 434); capture(w, 'cancel-draft')
        crop = (363, 417, 660, 605)
        intact = Image.open(out/'cancel-before.png').convert('RGB').crop(crop)
        removed = Image.open(out/'cancel-draft.png').convert('RGB').crop(crop)
        assert ImageChops.difference(intact, removed).getbbox() is not None, 'Cancel test must actually change the draft'
        assert layout.read_bytes() == saved, 'Draft must not save implicitly'
        click(w, 820, 110); capture(w, 'cancelled')
        assert layout.read_bytes() == saved
        close(w)
        w = start('reload-after-cancel'); capture(w, 'reloaded-after-cancel')
        assert layout.read_bytes() == saved and settings.read_bytes() == settings_before
        # IoT slot must still match its pre-cancel rendering.
        crop = (363, 407, 660, 575)
        first = Image.open(out/'reloaded.png').convert('RGB').crop(crop)
        final = Image.open(out/'reloaded-after-cancel.png').convert('RGB').crop(crop)
        assert ImageChops.difference(first, final).getbbox() is None
        close(w)
        result['checks'].append({'name': 'cancelled_removal_never_written_or_reloaded', 'status': 'passed'})
        if a.corrupt_inputs:
            bad_inputs = {
                'duplicate-schema': saved.replace(b'"schema":1', b'"schema":2,"schema":1'),
                'nul-tail': saved + b'\0future-layout-data',
            }
            for label, data in bad_inputs.items():
                assert data != saved
                layout.write_bytes(data)
                w = start('corrupt-' + label)
                assert layout.read_bytes() == data, 'Startup must preserve rejected data'
                click(w, 900, 84)
                click(w, 328, 434)
                click(w, 932, 110)
                capture(w, 'rejected-' + label)
                assert layout.read_bytes() == data, 'Save must not rewrite ambiguous data'
                assert settings.read_bytes() == settings_before
                close(w)
                result['checks'].append({'name': 'actual_GUI_reject_' + label,
                                         'status': 'passed',
                                         'original_sha256': hashlib.sha256(data).hexdigest()})
            layout.write_bytes(saved)  # Only this test's isolated data directory.

        result['settings_sha256'] = hashlib.sha256(settings_before).hexdigest()
        result['desktop_sha256'] = hashlib.sha256((a.release/'bin/lvglsim').read_bytes()).hexdigest()
        result['status'] = 'passed'
    except Exception as exc:
        result.update(status='failed', error=repr(exc))
        raise
    finally:
        if session and session.poll() is None:
            session.terminate()
            try: session.wait(timeout=8)
            except subprocess.TimeoutExpired: session.kill(); session.wait(timeout=3)
        if xvfb and xvfb.poll() is None:
            xvfb.terminate(); xvfb.wait(timeout=5)
        if rd >= 0: os.close(rd)
        if wr >= 0: os.close(wr)
        for log in logs: log.close()
        (out/'result.json').write_text(json.dumps(result, indent=2)+'\n')


if __name__ == '__main__': main()
