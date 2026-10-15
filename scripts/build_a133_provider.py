#!/usr/bin/env python3
"""Build the frozen A133 system provider from the SDK, never desktop sources."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

PREFIX = 'openwrt/openwrt/package/allwinner/custom/smartdesktopmimi-a133-provider'
TOOL_HASH = 'd2fc9477c990642fa27b56f65512ee751e5b3a1a478e5011b0f4878c0b56b4e0'
RECIPES = {
    'private': 'ab6e835a6062be717aa19c0b3634a48249a2ec77f3b4f0405b363776f65e883b',
    'public': 'f005cb9f5ddb614a0fa27e040e787e8838743a123792d6764b449a27eb6089e3',
}
COMPILER = 'prebuilt/rootfsbuilt/aarch64/toolchain-sunxi-glibc-gcc-1130/toolchain/bin/aarch64-openwrt-linux-gcc'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inside(path, root):
    return path == root or root in path.parents


def canonical(value):
    p = Path(value)
    if not p.is_absolute() or p != p.resolve() or any(c.isspace() for c in str(p)):
        raise ValueError('absolute canonical paths without whitespace required')
    return p


def pinned_file(path, root, digest):
    if not inside(path, root) or path.resolve() != path or not path.is_file() or sha(path) != digest:
        raise ValueError('missing or changed provider input: ' + str(path))


def preflight(sdk_value, profile, private_value=None):
    sdk = canonical(sdk_value)
    if not (sdk / '.repo').is_dir() or profile not in RECIPES:
        raise ValueError('wrong SDK root or cloud profile')
    component = sdk / PREFIX
    tool = component / 'build_provider.py'
    pinned_file(tool, sdk, TOOL_HASH)
    recipe_file = component / profile / 'provider-recipe.json'
    pinned_file(recipe_file, component, RECIPES[profile])
    recipe = json.loads(recipe_file.read_text())
    if recipe['cloud_profile'] != profile or recipe['tool_sha256'] != TOOL_HASH:
        raise ValueError('provider profile/tool mismatch')
    private = canonical(private_value) if profile == 'private' and private_value else sdk.parent / '.mimi-private-disabled'
    if profile == 'private':
        product = Path(__file__).resolve().parents[1]
        if not private.is_dir() or inside(private, sdk) or inside(private, product):
            raise ValueError('private profile requires an external private SDK')
    code = component / profile / 'code'
    for name, digest in recipe['sources'].items():
        pinned_file(code / name, code, digest)
    for name, digest in recipe['inputs'].items():
        bucket, rel = name.split('/', 1)
        if bucket == 'project':
            continue
        if bucket == 'private' and profile != 'private':
            raise ValueError('public provider references private SDK')
        root = {'sdk': sdk, 'private': private}[bucket]
        pinned_file(root / rel, root, digest)
    compiler = sdk / COMPILER
    target = compiler.resolve(strict=True)
    if not inside(target, sdk) or target.relative_to(sdk).as_posix() != recipe['compiler_target'] or sha(target) != recipe['compiler_sha256']:
        raise ValueError('provider compiler mismatch')
    return sdk, component, tool, recipe, private, compiler


def build(args):
    sdk, component, tool, recipe, private, compiler = preflight(args.sdk, args.profile, args.private_sdk)
    receipt = dict(requirement='REQ-MIMI-SPLIT-054', status='passed', phase='preflight',
                   profile=args.profile, recipe_sha256=RECIPES[args.profile], tool_sha256=TOOL_HASH,
                   provider_source_commit=recipe['source_commit'], release_qualified=False)
    if args.check:
        return receipt
    if not args.output:
        raise ValueError('--output is required for build')
    output = canonical(args.output)
    if output.exists() or inside(output, sdk) or (args.profile == 'private' and inside(output, private)):
        raise ValueError('build output must be a new directory outside SDK inputs')
    subprocess.run([sys.executable, str(tool), 'build', '--component', str(component / args.profile),
                    '--sdk', str(sdk), '--private-sdk', str(private), '--compiler', str(compiler),
                    '--output', str(output)], check=True)
    result = json.loads((output / 'build-result.json').read_text())
    if result['status'] != 'passed' or result['cloud_profile'] != args.profile or result['recipe_sha256'] != RECIPES[args.profile] or not result['no_original_source_dependency'] or result['backend_sha256'] != sha(output / 'lv_backend') or not result['byte_identical']:
        raise ValueError('independent provider build result mismatch')
    # Recheck all pinned inputs before allowing the caller to collect this binary.
    preflight(args.sdk, args.profile, args.private_sdk)
    receipt.update(phase='compiled', backend_sha256=result['backend_sha256'],
                   translation_units=result['translation_units'], original_source_dependency=False)
    (output / 'product-provider-result.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', required=True)
    parser.add_argument('--profile', choices=sorted(RECIPES), required=True)
    parser.add_argument('--private-sdk')
    parser.add_argument('--output')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    try:
        print(json.dumps(build(args)))
    except (ValueError, KeyError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, 'A133 SDK PROVIDER BLOCKED: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
