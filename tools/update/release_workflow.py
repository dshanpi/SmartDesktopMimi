#!/usr/bin/env python3
"""Controlled runner entry; input strings are data, never shell commands."""
import argparse
import json
import os
from pathlib import Path
from release_identity import PUBLIC_REPOSITORY, parse_tag
from release_gate import check
from github_release import GitHubRelease
from publish_draft import publish
from promote_release import promote

def candidate(root, tag, stage):
    parse_tag(tag)
    if stage not in {'draft','prerelease','stable'}:
        raise ValueError('invalid release tag/stage')
    directory=root/tag
    if directory.is_symlink() or directory.resolve().parent!=root.resolve() or not directory.is_dir():
        raise ValueError('candidate is absent or outside the configured release root')
    return directory.resolve()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True)
    p.add_argument('--checker',type=Path,required=True)
    p.add_argument('--trust',type=Path,required=True)
    a=p.parse_args()
    tag=os.environ.get('MIMI_RELEASE_TAG','');stage=os.environ.get('MIMI_RELEASE_STAGE','')
    directory=candidate(a.root,tag,stage)
    checker=a.checker.resolve();trust=a.trust.resolve()
    lock=check(directory,stage,checker,trust)
    if lock['tag']!=tag or lock['release_type']!=parse_tag(tag)[0]:raise ValueError('candidate tag/type differs from requested release')
    client=GitHubRelease(PUBLIC_REPOSITORY,os.environ.get('GITHUB_TOKEN'))
    result=publish(directory,checker,trust,client) if stage=='draft' else promote(directory,stage,checker,trust,client)
    print(json.dumps(result,ensure_ascii=False))

if __name__=='__main__':main()
