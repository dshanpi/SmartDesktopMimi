#!/usr/bin/env python3
"""Build the pinned A133 SDK provider with explicit product pairing components."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

PREFIX = 'openwrt/openwrt/package/allwinner/custom/smartdesktopmimi-a133-provider'
TOOL_HASH = 'd2fc9477c990642fa27b56f65512ee751e5b3a1a478e5011b0f4878c0b56b4e0'
RECIPES = {
    'private': 'fded4477968ca3070806597f8deb868741fac83ee68b5895f4eed79ccf35364c',
    'public': 'e40509021c45d869d6ed735e8e5fe93df0a061eb4e9ab836c9010cf76780f654',
}
EDITION = 'edition-v3'
AI_SOURCE = 'apps/lv_port_linux/src/system/service_ai_runtime.c'
READER_SOURCE = 'apps/lv_port_linux/src/system/mimi_tuya_license_provider.h'
TUYA_LOCK = 'adapters/tuya/source-lock.json'
TUYA_LOCK_HASH = '7392aac54a9f2c70d6404aa8ae3690b03784fb3d882bab0e85a574ff42e15b6c'
TUYA_READER = 'adapters/tuya/mimi_tuya_license_provider.h'
COMPILER = 'prebuilt/rootfsbuilt/aarch64/toolchain-sunxi-glibc-gcc-1130/toolchain/bin/aarch64-openwrt-linux-gcc'
PRIVATE_LOCK = 'adapters/100ask/source-lock.json'
PRIVATE_LOCK_HASH = 'f6937aa9ffa903a5416bfd4762e720ab5c8f50d6753bbfbf5fc217ac8f634631'
PRIVATE_SOURCES = {
    'apps/lv_port_linux/src/system/service_cloud.c': 'adapters/100ask/system/service_cloud.c',
    'apps/lv_port_linux/src/system/service_cloud_validation.c': 'adapters/100ask/system/service_cloud_validation.c',
    'apps/lv_port_linux/src/system/pairing_response.h': 'adapters/100ask/system/pairing_response.h',
}


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


def private_component(value, sdk, private, recipe):
    if not value:
        raise ValueError('private profile requires an explicit private product component')
    root = canonical(value)
    public = Path(__file__).resolve().parents[1]
    if (not root.is_dir() or inside(root, sdk) or inside(root, public) or
            inside(root, private) or inside(sdk, root) or inside(public, root) or inside(private, root)):
        raise ValueError('private product component must be separate from SDK and public source')
    pinned_file(root / PRIVATE_LOCK, root, PRIVATE_LOCK_HASH)
    lock = json.loads((root / PRIVATE_LOCK).read_text())
    if (lock.get('schema') != 1 or lock.get('component') != '100ask-auto-pairing' or
            lock.get('composition') != PRIVATE_SOURCES or
            set(lock.get('files', {})) != set(PRIVATE_SOURCES.values()) or
            lock.get('provider_recipe_sha256') != RECIPES['private']):
        raise ValueError('private component source composition or provider recipe mismatch')
    for name, relative in PRIVATE_SOURCES.items():
        item = lock['files'][relative]
        source = root / relative
        if item['sha256'] != recipe['sources'].get(name):
            raise ValueError('private component differs from fixed provider source')
        pinned_file(source, root, item['sha256'])
        mode = '100755' if source.stat().st_mode & 0o111 else '100644'
        if source.stat().st_size != item['bytes'] or mode != item['git_mode']:
            raise ValueError('private component source size or Git mode mismatch')
    pinned_file(root / TUYA_LOCK, root, TUYA_LOCK_HASH)
    tuya = json.loads((root / TUYA_LOCK).read_text())
    if (tuya.get('schema') != 1 or tuya.get('component') != 'tuya-device-pairing' or
            tuya.get('api_version') != 1 or
            set(tuya.get('files', {})) != {TUYA_READER, 'adapters/tuya/mimi_device_pairing.c'}):
        raise ValueError('private Tuya reader component mismatch')
    for name, item in tuya['files'].items():
        pinned_file(root / name, root, item['sha256'])
        mode = '100755' if (root / name).stat().st_mode & 0o111 else '100644'
        if mode != item['git_mode'] or (root / name).stat().st_size != item['bytes']:
            raise ValueError('private Tuya component source size or Git mode mismatch')
    if tuya['files'][TUYA_READER]['sha256'] != recipe['sources'].get(READER_SOURCE):
        raise ValueError('private Tuya reader differs from fixed provider source')
    return root, lock, tuya


def selected_source(component, profile, name, product):
    # SDK's historical private copies are never selected for these two files.
    if product and name in PRIVATE_SOURCES:
        return product[0] / PRIVATE_SOURCES[name], product[0]
    if name in (AI_SOURCE, 'apps/lv_port_linux/src/middleware/middleware.h', 'apps/lv_port_linux/src/system/cloud_pairing_status.h'):
        return component / EDITION / 'code' / name, component / EDITION / 'code'
    if name == READER_SOURCE:
        if product:
            return product[0] / TUYA_READER, product[0]
        return component / EDITION / 'public/mimi_tuya_license_provider.h', component / EDITION / 'public'
    code = component / profile / 'code'
    return code / name, code


def source_graph(recipe):
    graph = {key: recipe[key] for key in ('sources', 'inputs', 'translation_units',
                                        'link_flags', 'compiler_target', 'compiler_sha256')}
    return hashlib.sha256(json.dumps(graph, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def preflight(sdk_value, profile, private_value=None, product_value=None):
    if profile == 'public' and (private_value or product_value):
        raise ValueError('public profile forbids private SDK or product inputs')
    sdk = canonical(sdk_value)
    if not (sdk / '.repo').is_dir() or profile not in RECIPES:
        raise ValueError('wrong SDK root or cloud profile')
    component = sdk / PREFIX
    tool = component / 'build_provider.py'
    pinned_file(tool, sdk, TOOL_HASH)
    recipe_file = component / EDITION / profile / 'provider-recipe.json'
    pinned_file(recipe_file, component, RECIPES[profile])
    recipe = json.loads(recipe_file.read_text())
    if recipe['cloud_profile'] != profile or recipe['tool_sha256'] != TOOL_HASH:
        raise ValueError('provider profile/tool mismatch')
    if (recipe.get('edition_source_version') != 3 or
            recipe.get('edition_source_requirement') != 'REQ-MIMI-PAIR-071' or
            recipe.get('reference_state') != 'audited_new_source_reference' or
            recipe.get('source_graph_sha256') != source_graph(recipe) or
            len(recipe.get('reference_backend_sha256', '')) != 64 or
            READER_SOURCE not in recipe['sources'] or AI_SOURCE not in recipe['sources']):
        raise ValueError('provider edition/source graph/reference mismatch')
    private = canonical(private_value) if profile == 'private' and private_value else sdk.parent / '.mimi-private-disabled'
    if profile == 'private':
        product = Path(__file__).resolve().parents[1]
        if not private.is_dir() or inside(private, sdk) or inside(private, product):
            raise ValueError('private profile requires an external private SDK')
    product_component = private_component(product_value, sdk, private, recipe) if profile == 'private' else None
    for name, digest in recipe['sources'].items():
        source, source_root = selected_source(component, profile, name, product_component)
        pinned_file(source, source_root, digest)
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
    return sdk, component, tool, recipe, private, compiler, product_component


def compose(component, recipe, product, directory):
    """Copy only fixed source blobs; execute the original, pinned SDK builder."""
    profile = recipe['cloud_profile']
    composed = directory / profile
    (composed / 'code').mkdir(parents=True)
    shutil.copyfile(component / EDITION / profile / 'provider-recipe.json', composed / 'provider-recipe.json')
    for name, digest in recipe['sources'].items():
        source, source_root = selected_source(component, profile, name, product)
        pinned_file(source, source_root, digest)
        target = composed / 'code' / name
        if not inside(target, composed / 'code') or target.resolve() != target:
            raise ValueError('unsafe composed source path')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        pinned_file(target, composed / 'code', digest)
    return composed


def build(args):
    sdk, component, tool, recipe, private, compiler, product = preflight(
        args.sdk, args.profile, args.private_sdk, args.private_product_root)
    receipt = dict(requirement='REQ-MIMI-PRIVATE-066', status='passed', phase='preflight',
                   profile=args.profile, recipe_sha256=RECIPES[args.profile], tool_sha256=TOOL_HASH,
                   provider_source_commit=recipe['source_commit'], release_qualified=False,
                   provider_edition_source_version=3, source_graph_sha256=recipe['source_graph_sha256'],
                   reference_backend_sha256=recipe['reference_backend_sha256'],
                   prior_backend_sha256=recipe['prior_backend_sha256'])
    if product:
        receipt.update(private_product_source_lock_sha256=PRIVATE_LOCK_HASH,
                       private_tuya_source_lock_sha256=TUYA_LOCK_HASH,
                       private_adapter_origin_sdk_commit=product[1]['source_commit'],
                       private_adapter_sources={name: recipe['sources'][name] for name in PRIVATE_SOURCES},
                       historical_sdk_pairing_sources_used=False)
    if args.check:
        return receipt
    if not args.output:
        raise ValueError('--output is required for build')
    output = canonical(args.output)
    if (output.exists() or inside(output, sdk) or
            (args.profile == 'private' and (inside(output, private) or inside(output, product[0])))):
        raise ValueError('build output must be a new directory outside SDK inputs')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.provider-source-', dir=output.parent) as temp:
        selected = compose(component, recipe, product, Path(temp))
        subprocess.run([sys.executable, str(tool), 'build', '--component', str(selected),
                        '--sdk', str(sdk), '--private-sdk', str(private), '--compiler', str(compiler),
                        '--output', str(output)], check=True)
    result = json.loads((output / 'build-result.json').read_text())
    if (result['status'] != 'passed' or result['cloud_profile'] != args.profile or
            result['recipe_sha256'] != RECIPES[args.profile] or not result['no_original_source_dependency'] or
            result['backend_sha256'] != sha(output / 'lv_backend') or
            result['backend_sha256'] != recipe['reference_backend_sha256'] or
            result['original_backend_sha256'] != recipe['prior_backend_sha256'] or
            result['byte_identical'] != (result['backend_sha256'] == recipe['prior_backend_sha256'])):
        raise ValueError('independent provider build result mismatch')
    # Recheck all pinned inputs before allowing the caller to collect this binary.
    preflight(args.sdk, args.profile, args.private_sdk, args.private_product_root)
    receipt.update(phase='compiled', backend_sha256=result['backend_sha256'],
                   translation_units=result['translation_units'], original_source_dependency=False,
                   byte_identical_to_prior=result['byte_identical'])
    (output / 'product-provider-result.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', required=True)
    parser.add_argument('--profile', choices=sorted(RECIPES), required=True)
    parser.add_argument('--private-sdk')
    parser.add_argument('--private-product-root')
    parser.add_argument('--output')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    try:
        print(json.dumps(build(args)))
    except (ValueError, KeyError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, 'A133 SDK PROVIDER BLOCKED: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
