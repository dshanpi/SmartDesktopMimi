#!/usr/bin/env python3
"""Inventory exposed UI sources and callback bindings without claiming execution."""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
CATALOG=ROOT/'tests/update/ui-catalog.json'
SCENARIOS={
 'desktop':['topbar-hit-target','control-center','inline-app-routing','all-apps','inline-horizontal-pages','page-line-indicators','no-duplicate-footer','back-navigation','modal-focus'],
 'widgets':['default-five-slots','library-compatible-sizes','draft-cancel','save-persistence-error','concurrent-save-conflict','corrupt-or-future-layout','long-press-edit','same-size-drag','reject-cross-size-drag','small-screen-cross-viewport-drag','environment-details','environment-stale-recovery','iot-summary-empty-error-expired','no-home-cloud-poll'],
 'control_center':['brightness-volume-readback-save','mute-restore','hdmi-ack-before-save','hdmi-timeout-restore','readonly-update-notification-navigation','short-screen-scroll-controls','dismiss-no-touch-through'],
 'setting':['sliders-persist','language-theme','network-navigation','application-update','system-update'],
 'wifi':['scan','wrong-password','connect','disconnect','reconnect','cancel'],
 'bt':['enable-disable','scan-pair','rename-cancel','audio-playback','progress','reconnect'],
 'video':['empty-library','long-name','play-pause','seek','end-of-media','remove-media'],
 'ai':['wake-nihao-tuya','speech-recognition','speech-output','external-barge-in','reject-speaker-echo','offline-recovery'],
 'ota':['check','download','cancel-download','apply','error','back-to-settings'],
 'user':['empty-list','install','invalid-signature','permissions','launch-exit','upgrade-data','uninstall','platform-catalog-select-reopen','invalid-platform-config','disabled-service','unavailable-service'],
 'hdmi_mcp':['source-preview','hotplug','task-state','cancel','keyboard-mouse','release-on-disconnect'],
 'pairing':['existing-bound-no-token','refresh','new-code','expiry','offline','no-credential-display','authoritative-bound','unknown-ownership','reject-optional-schema','same-code-poll-no-renewal','bound-requery-after-website-unlink'],
 'iot':['unconfigured','device-list','device-status','confirm-control','provider-error','control-readback'],
 'voice':['optional-disabled','text-edit','transcript-preview','confirm-command','cancel','unsafe-command-rejection','real-speech'],
 'web':['login','same-origin','settings','video','keyboard-mouse','blur-release','mobile-layout','apps','update'],
}

def inventory():
    result={}
    paths=list((ROOT/'apps/lv_port_linux/src/apps').rglob('*.c'))
    paths += list((ROOT/'apps/lv_port_linux/src/ui').glob('*.c'))
    paths += list((ROOT/'apps/ipkvm/upstream/ui/src/layout').rglob('*.tsx'))
    for path in sorted(paths):
        if 'lockscreen' in path.name:
            continue
        text=path.read_text()
        if path.suffix=='.c':
            callbacks=re.findall(r'lv_obj_add_event_cb\s*\((.*?)\)\s*;',text,re.S)
        else:
            callbacks=re.findall(r'\b(onClick|onChange|onSubmit|onKeyDown|onPointerDown|onPointerUp|onBlur|onFocus)\s*=',text)
        if not callbacks:
            continue
        relative=path.relative_to(ROOT).as_posix()
        # Contents bind this inventory to the reviewed page, including handler body changes.
        result[relative]={'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                          'bindings':[' '.join(c.split()) for c in callbacks]}
    registry=(ROOT/'apps/lv_port_linux/src/system/app_registry.c').read_text()
    apps=sorted(set(re.findall(r'\.id\s*=\s*(APP_ID_\w+)',registry)))
    return {'schema_version':1,'application_ids':apps,'surfaces':result,'scenarios':SCENARIOS,
            'required_states':['normal','loading','empty','error','long-text','repeat-operation'],
            'variants':['zh-light','zh-dark','en-light','en-dark'],
            'note':'Inventory only. Physical results require separate artifact-bound evidence.'}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--record',action='store_true',help='explicitly review and record a changed UI inventory')
    p.add_argument('--report',type=Path)
    a=p.parse_args();actual=inventory()
    if a.record:
        CATALOG.write_text(json.dumps(actual,ensure_ascii=False,indent=2)+'\n')
    elif json.loads(CATALOG.read_text()) != actual:
        p.exit(1,'UI inventory changed: review new/changed bindings and scenarios before --record\n')
    if a.report:
        report={'schema_version':1,'kind':'test-inventory','results':[
            {'id':area+'.'+case,'result':'not_run','method':'physical-device' if area in {'ai','wifi','bt','video','hdmi_mcp','pairing','iot','voice'} else 'unassigned'}
            for area,cases in SCENARIOS.items() for case in cases]}
        a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS inventory: {len(actual["application_ids"])} applications, {len(actual["surfaces"])} interactive sources; no hardware execution implied')


if __name__=='__main__':main()
