"""Explicit SDK input for platform implementation tests after source separation.

No SDK configured means that specific implementation check was not run.
A configured but changed/incomplete SDK is an error, never a skip or fallback.
"""
import hashlib
import json
import os
from pathlib import Path
import unittest

RECIPES = {
    'private': 'ab6e835a6062be717aa19c0b3634a48249a2ec77f3b4f0405b363776f65e883b',
    'public': 'f005cb9f5ddb614a0fa27e040e787e8838743a123792d6764b449a27eb6089e3',
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def provider_source(name, profile='private'):
    value = os.environ.get('MIMI_A133_PROVIDER_ROOT')
    if not value:
        raise unittest.SkipTest('A133 SDK implementation not_run: set MIMI_A133_PROVIDER_ROOT')
    root = Path(value)
    if not root.is_absolute() or root != root.resolve():
        raise ValueError('configured A133 provider must be an absolute canonical directory')
    recipe_path = root / profile / 'provider-recipe.json'
    if recipe_path.resolve() != recipe_path or digest(recipe_path) != RECIPES[profile]:
        raise ValueError('configured A133 provider recipe differs from the fixed SDK')
    recipe = json.loads(recipe_path.read_text())
    path = root / profile / 'code' / name
    if name not in recipe['sources'] or path.resolve() != path or not path.is_file() or digest(path) != recipe['sources'][name]:
        raise ValueError('configured A133 provider implementation differs: ' + name)
    return path
