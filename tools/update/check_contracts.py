#!/usr/bin/env python3
"""Freeze the currently supported native IPC/Ports headers until explicit review."""
import hashlib
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]

def main():
    contract=json.loads((ROOT/'docs/update/platform-contract-lock.json').read_text())
    for name,expected in contract['headers'].items():
        actual=hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
        if actual != expected:
            raise SystemExit(f'CONTRACT REVIEW REQUIRED: {name}; update the contract version/compatibility evidence before recording a new lock')
    print('PASS frozen platform contract:',contract['contract_id'])

if __name__=='__main__':main()
