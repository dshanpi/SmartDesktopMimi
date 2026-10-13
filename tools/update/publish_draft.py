#!/usr/bin/env python3
"""Upload a gated release draft and re-run admission on downloaded assets."""
import argparse
import json
import os
from pathlib import Path
import tempfile
import urllib.parse
from release_gate import require_upload_acceptance, check, sha
from github_release import GitHubRelease, verify_repository, verify_remote, asset_snapshot


def publish(directory, checker, trust, client):
    lock = check(directory,'draft',checker,trust)
    require_upload_acceptance(directory,lock)
    verify_repository(lock,client.repository)
    client.verify_tag(lock)
    release = client.find(lock['tag'])
    if release is None:
        release = client.api('/releases','POST',dict(tag_name=lock['tag'],target_commitish=lock['source_commit'],
            name='SmartDesktopMimi '+lock['tag'],draft=True,prerelease=True,
            body='Candidate draft. See validation evidence; publication requires remote verification.'))
    if not release['draft']: raise ValueError('refusing to replace a published release')
    existing = client.assets(release)
    expected = {**lock['assets'],'release-lock.json':sha(directory/'release-lock.json')}
    if not set(existing)<=set(expected): raise ValueError('unexpected assets in existing draft')
    for name in expected:
        if name in existing: continue
        url = release['upload_url'].split('{',1)[0]+'?name='+urllib.parse.quote(name,safe='')
        with (directory/name).open('rb') as source:
            with client.request(url,'POST',source,'application/octet-stream') as response:
                response.read(1<<20)
    with tempfile.TemporaryDirectory(prefix='mimi-remote-draft-') as temporary:
        remote = Path(temporary)
        snapshot = verify_remote(client,release,directory,lock,remote)
        remote_lock = check(remote,'draft',checker,trust)
        require_upload_acceptance(remote,remote_lock)
        client.verify_tag(lock)
        fresh = client.api('/releases/'+str(release['id']))
        if not fresh['draft'] or fresh['tag_name'] != lock['tag'] or snapshot != asset_snapshot(client.assets(fresh)):
            raise ValueError('remote draft changed during verification')
    return dict(draft_url=release['html_url'],assets_verified=len(expected))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory',type=Path)
    p.add_argument('--checker',type=Path,required=True)
    p.add_argument('--trust',type=Path,required=True)
    p.add_argument('--repository',default='dshanpi/SmartDesktopMimi')
    a=p.parse_args()
    print(json.dumps(publish(a.directory.resolve(),a.checker.resolve(),a.trust.resolve(),
        GitHubRelease(a.repository,os.environ.get('GITHUB_TOKEN'))),ensure_ascii=False))


if __name__=='__main__':main()
