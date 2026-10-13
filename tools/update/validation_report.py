#!/usr/bin/env python3
"""Summarize actual host evidence while keeping physical acceptance separate."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import re
from ui_catalog import SCENARIOS

ROOT=Path(__file__).resolve().parents[2]

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('evidence',type=Path);a=p.parse_args();out=a.evidence.resolve()
    checkpoint=json.loads((out/'source-checkpoint.json').read_text())
    pipeline=json.loads((out/'host-package-fixture/result.json').read_text())
    if checkpoint['commit']!=pipeline['source_checkpoint']:p.error('pipeline/source mismatch')
    go=[json.loads(line) for line in (out/'updater-tests.jsonl').read_text().splitlines()]
    if any(e['Action']=='fail' for e in go):p.error('updater test failed')
    passed=[e['Test'] for e in go if e['Action']=='pass' and 'Test' in e]
    if not passed:p.error('no updater test evidence')
    test_counts={}
    for name in ['host-integration','architecture-tests','release-tests']:
        log=(out/(name+'.log')).read_text()
        match=re.search(r'Ran (\d+) tests',log)
        if not match or not log.rstrip().endswith('OK'):p.error('incomplete/failed suite: '+name)
        test_counts[name]=int(match[1])
    ui=(out/'ui-tests.log').read_text();match=re.search(r'PASS: (\d+) LVGL renders',ui)
    if not match:p.error('missing native UI completion')
    ui_count=int(match[1]);manager=(out/'ui/manager.log').read_text()
    if 'PASS 100 host pointer rounds across all 8 launcher apps' not in manager:p.error('missing navigation completion')
    # Deliberately no physical passed results are inferred from host fixtures.
    host_ui={'desktop.topbar-hit-target','desktop.control-center','desktop.dock-routing','desktop.all-apps','desktop.back-navigation',
             'setting.sliders-persist','setting.language-theme','setting.application-update','bt.rename-cancel',
             'video.empty-library','video.long-name','ota.error'}
    if (out/'web-interaction.json').is_file():host_ui.update({'web.mobile-layout','web.update'})
    if 'PASS OTA: download cancel routes correctly' in (out/'ui/ota-downloading-en-light.log').read_text():host_ui.add('ota.cancel-download')
    host_function={'user.install','user.invalid-signature','user.permissions','user.launch-exit','user.upgrade-data','user.uninstall','hdmi_mcp.task-state','hdmi_mcp.cancel','hdmi_mcp.release-on-disconnect'}
    scenarios=[]
    for area,cases in SCENARIOS.items():
        for case in cases:
            name=area+'.'+case
            covered=name in host_ui or name in host_function
            scenarios.append({'id':name,'host':{'result':'passed' if covered else 'not_run','method':'host-fixture' if covered else 'unassigned',
               'scope':'UI validation only; real hardware behavior remains untested' if name in host_ui else 'isolated protocol/service fixture' if name in host_function else ''},
               'physical':{'result':'not_run','reason':'Current A133 maintenance connection unavailable; prior board stability faults remain unresolved'}})
    logs={str(f.relative_to(out)):sha(f) for f in out.rglob('*.log') if f.is_file()}
    logs['updater-tests.jsonl']=sha(out/'updater-tests.jsonl')
    result={'schema_version':1,'source_checkpoint':checkpoint['commit'],'release_status':'blocked; no system image or GitHub release published',
        'host_suites':test_counts,'updater_test_cases_including_subtests':len(passed),'native_ui_renders':ui_count,
        'native_pointer_navigation':{'rounds':100,'applications':8,'entries':800,'method':'host LVGL pointer fixture'},
        'package_pipeline':pipeline,'scenarios':scenarios,'evidence_sha256':logs,'note':'A page rendered or a mocked command passed does not certify all functions of that page.'}
    observation=out/'board-observation.json'
    if observation.is_file():
        result['board_observation']=json.loads(observation.read_text())
        logs['board-observation.json']=sha(observation)
    (out/'validation-summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    rows=''.join('<tr><td>'+html.escape(s['id'])+'</td><td>'+s['host']['result']+'</td><td>not_run</td></tr>' for s in scenarios)
    cards=''.join('<figure><img loading="lazy" src="ui/'+html.escape(f.name)+'"><figcaption>'+html.escape(f.stem)+'</figcaption></figure>' for f in sorted((out/'ui').glob('*.png')))
    page='''<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>SmartDesktopMimi 验证记录</title>
<style>body{font:16px/1.6 system-ui;background:#f4f6fa;color:#17212e;margin:24px auto;max-width:1280px;padding:0 20px}h1{font-size:30px}.notice{padding:18px;background:#fff0d6;border-radius:12px}table{border-collapse:collapse;width:100%;background:white}td,th{text-align:left;padding:8px;border-bottom:1px solid #dde3eb}.gallery{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:16px}figure{margin:0;background:white;border-radius:12px;overflow:hidden}img{width:100%;display:block}figcaption{padding:12px}input{font:inherit;padding:10px;margin:16px 0;width:90%}</style>
<h1>SmartDesktopMimi 验证记录</h1><p class="notice">这是主机验证记录。实机功能、升级回退、断电恢复及稳定性尚未验收；未发布系统镜像或 GitHub Release。主机测试包使用虚拟硬件身份，禁止部署。</p>
<p>原生页面图 '''+str(ui_count)+''' 张；8 个应用 × 100 轮指针导航；详细日志哈希和逐项结果见 <a href="validation-summary.json">JSON 记录</a>。</p>
<input id="filter" placeholder="筛选页面或功能，例如 wifi、ai、update"><table><thead><tr><th>功能场景</th><th>主机限定范围</th><th>实机</th></tr></thead><tbody>'''+rows+'''</tbody></table><h2>真实 LVGL 渲染</h2><div class="gallery">'''+cards+'''</div><script>document.querySelector('#filter').oninput=e=>{const q=e.target.value.toLowerCase();document.querySelectorAll('tbody tr,figure').forEach(n=>n.hidden=!n.textContent.toLowerCase().includes(q))}</script>'''
    (out/'index.html').write_text(page)
    print(json.dumps({'host_suites':test_counts,'updater_cases':len(passed),'ui_renders':ui_count,'physical':'not_run','release':'not_published'}))
if __name__=='__main__':main()
