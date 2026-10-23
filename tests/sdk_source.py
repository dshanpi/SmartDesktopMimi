"""Explicit SDK input for platform implementation tests after source separation.

No SDK configured means that specific implementation check was not run.
A configured but changed/incomplete SDK is an error, never a skip or fallback.
"""
import hashlib
import json
import os
from pathlib import Path
import unittest

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import build_a133_provider as provider


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def provider_source(name, profile='private'):
    value = os.environ.get('MIMI_A133_PROVIDER_ROOT')
    if not value:
        raise unittest.SkipTest('A133 SDK implementation not_run: set MIMI_A133_PROVIDER_ROOT')
    component = provider.canonical(value)
    suffix = Path(provider.PREFIX).parts
    if component.parts[-len(suffix):] != suffix:
        raise ValueError('configured provider must be the selected SDK component root')
    sdk = component.parents[len(suffix)-1]
    private = os.environ.get('MIMI_A133_PRIVATE_SDK') if profile == 'private' else None
    product = os.environ.get('MIMI_PRIVATE_PRODUCT_ROOT') if profile == 'private' else None
    _, selected_component, _, recipe, _, _, selected_product = provider.preflight(str(sdk), profile, private, product)
    if name not in recipe['sources']:
        raise ValueError('source is not part of selected provider: ' + name)
    path, source_root = provider.selected_source(selected_component, profile, name, selected_product)
    provider.pinned_file(path, source_root, recipe['sources'][name])
    return path


SERVICES_LOCK_SHA256 = '03cd9bd5e9b1661be82dc78f39606b6a65189b59f107ece6722b5d94982d3f58'


def services_source(name):
    value = os.environ.get('MIMI_A133_SERVICES_ROOT')
    if not value:
        raise unittest.SkipTest('A133 platform-services implementation not_run: set MIMI_A133_SERVICES_ROOT')
    root = Path(value)
    if not root.is_absolute() or root != root.resolve():
        raise ValueError('configured platform-services root must be absolute and canonical')
    lock_path = root / 'source-lock.json'
    if lock_path.resolve() != lock_path or digest(lock_path) != SERVICES_LOCK_SHA256:
        raise ValueError('configured platform-services lock changed')
    lock = json.loads(lock_path.read_text())
    path = root / 'code' / name
    if path.resolve() != path or lock['files'].get('code/' + name) != digest(path):
        raise ValueError('configured platform service source changed')
    return path


HDMI_LOCK_SHA256 = '7c6a78259a3dbb6b3b027235c7536349f6b50555c0aedecf9ec5bcf5cc05bdc5'


def hdmi_source(name):
    value = os.environ.get('MIMI_A133_HDMI_ROOT')
    if not value:
        raise unittest.SkipTest('A133 HDMI implementation not_run: set MIMI_A133_HDMI_ROOT')
    root = Path(value)
    if not root.is_absolute() or root != root.resolve():
        raise ValueError('configured HDMI root must be absolute and canonical')
    lock_path = root / 'source-lock.json'
    if lock_path.resolve() != lock_path or digest(lock_path) != HDMI_LOCK_SHA256:
        raise ValueError('configured HDMI lock changed')
    lock = json.loads(lock_path.read_text())
    path = root / 'code' / name
    if path.resolve() != path or lock['files'].get('code/' + name) != digest(path):
        raise ValueError('configured HDMI source changed')
    return path


CAPTURE_LOCK_SHA256 = 'e84ab80ea1c0a1618efd008abd6166acf0f560f3065bb76d3469370737a8575f'


def capture_source(name):
    value = os.environ.get('MIMI_A133_CAPTURE_ROOT')
    if not value:
        raise unittest.SkipTest('A133 capture implementation not_run: set MIMI_A133_CAPTURE_ROOT')
    root = Path(value)
    if not root.is_absolute() or root != root.resolve():
        raise ValueError('configured capture root must be absolute and canonical')
    lock_path = root / 'source-lock.json'
    if lock_path.resolve() != lock_path or digest(lock_path) != CAPTURE_LOCK_SHA256:
        raise ValueError('configured capture lock changed')
    lock = json.loads(lock_path.read_text())
    path = root / 'code' / name
    if path.resolve() != path or lock['files'].get('code/' + name) != digest(path):
        raise ValueError('configured capture source changed')
    return path
