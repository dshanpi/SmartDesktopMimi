#!/usr/bin/env python3
"""Promote only a draft whose downloaded bytes pass the publication gate."""
import argparse
import json
import os
from pathlib import Path
import tempfile
from release_gate import require_upload_acceptance, check
from github_release import GitHubRelease, verify_repository, verify_remote, asset_snapshot

def promote(directory, stage, checker, trust, client):
    if stage not in {'stable','prerelease'}: raise ValueError('explicit publication stage required')
    lock=check(directory,stage,checker,trust)
    require_upload_acceptance(directory,lock)
    verify_repository(lock,client.repository)
    client.verify_tag(lock)
    release=client.find(lock['tag'])
    if release is None or not release['draft']: raise ValueError('only an existing draft can be promoted')
    with tempfile.TemporaryDirectory(prefix='mimi-promote-') as temporary:
        remote=Path(temporary)
        snapshot=verify_remote(client,release,directory,lock,remote)
        remote_lock = check(remote,stage,checker,trust)
        require_upload_acceptance(remote,remote_lock)
        client.verify_tag(lock)
        fresh=client.api('/releases/'+str(release['id']))
        if not fresh['draft'] or fresh['tag_name']!=lock['tag'] or snapshot!=asset_snapshot(client.assets(fresh)):
            raise ValueError('remote release changed during admission')
        result=client.api('/releases/'+str(release['id']),'PATCH',dict(draft=False,prerelease=stage=='prerelease',
            make_latest='true' if stage=='stable' else 'false',
            body='SmartDesktopMimi '+lock['tag']+'\n\n发布附件已下载复核；目标、来源和验收结果见 release-lock.json 及各目标 validation.json。'))
    if result.get('draft') is not False or result.get('prerelease')!=(stage=='prerelease') or result.get('tag_name')!=lock['tag']:
        raise ValueError('publication response not confirmed; inspect remote before retry')
    return dict(url=result['html_url'],stage=stage,version=lock['version'])

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory',type=Path)
    p.add_argument('--stage',choices=['stable','prerelease'],required=True)
    p.add_argument('--checker',type=Path,required=True)
    p.add_argument('--trust',type=Path,required=True)
    p.add_argument('--repository',default='dshanpi/SmartDesktopMimi')
    a=p.parse_args()
    print(json.dumps(promote(a.directory.resolve(),a.stage,a.checker.resolve(),a.trust.resolve(),
        GitHubRelease(a.repository,os.environ.get('GITHUB_TOKEN'))),ensure_ascii=False))

if __name__=='__main__':main()
