#!/usr/bin/env python3
"""Render the real LVGL first-batch UI with isolated hardware/message fixtures.

Requires GCC, CMake, Pillow, and an existing A133-generated lv_conf.h.
No hardware, credentials, or network access is used by the review executable.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse
import re
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / 'apps/lv_port_linux'


def run(command, log):
    result = subprocess.run(list(map(str, command)), cwd=ROOT, stdout=log,
                            stderr=subprocess.STDOUT)
    if result.returncode:
        raise SystemExit(f'UI review command failed ({result.returncode}); see {log.name}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=APP / 'build/lv_conf.h')
    parser.add_argument('--output', type=Path, default=ROOT / 'output/lvgl-ui-v2-batch1')
    parser.add_argument('--pages', nargs='+', default=['home', 'control-center', 'apps', 'settings', 'desktop-update', 'wifi', 'password', 'bt', 'bt-connected', 'bt-details', 'bt-rename', 'video', 'video-library', 'ota', 'ota-error', 'ota-downloading', 'ai', 'ai-pairing', 'ai-offline', 'ai-speaking', 'ai-free-chat', 'user', 'hdmi'])
    parser.add_argument('--resolutions', nargs='+', default=['1024x768'],
                        help='Host render sizes only; does not qualify physical displays')
    parser.add_argument('--without-hdmi', action='store_true', help='Exercise unsupported build capability; no provider qualification')
    args = parser.parse_args()
    resolutions = []
    for value in args.resolutions:
        if not re.fullmatch(r'[0-9]{3,4}x[0-9]{3,4}', value):
            parser.error('resolution must be WIDTHxHEIGHT')
        w, h = map(int, value.split('x'))
        if not (480 <= w <= 1920 and 480 <= h <= 1920):
            parser.error('host review sizes must be between 480 and 1920')
        resolutions.append((w, h))
    build = ROOT / ('build/ui-review-no-hdmi' if args.without_hdmi else 'build/ui-review')
    capability_flags = ['-DMIMI_HDMI_SUPPORTED=0'] if args.without_hdmi else []
    build.mkdir(parents=True, exist_ok=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    config = args.config.read_text()
    for key, value in {'LV_USE_EVDEV': '0', 'LV_USE_LINUX_FBDEV': '0',
                       'LV_USE_SNAPSHOT': '1', 'LV_COLOR_DEPTH': '32',
                       'LV_USE_LOG': '0'}.items():
        config = re.sub(r'(#define ' + key + r'\s+)\S+', lambda m: m[1] + value, config)
    config_path = build / 'lv_conf.h'
    if not config_path.exists() or config_path.read_text() != config:
        config_path.write_text(config)
    with (output / 'build.log').open('w') as log:
        run(['cmake', '-S', APP / 'lvgl', '-B', build / 'lvgl',
             f'-DLV_BUILD_CONF_PATH={config_path}', '-DCONFIG_LV_BUILD_DEMOS=OFF',
             '-DCONFIG_LV_BUILD_EXAMPLES=OFF', '-DCONFIG_LV_USE_THORVG_INTERNAL=OFF',
             '-DCMAKE_BUILD_TYPE=Release'], log)
        run(['cmake', '--build', build / 'lvgl', '-j4'], log)
        sources = [ROOT / 'tests/architecture/fixtures/native_ui_review.c',
                   APP / 'src/apps/setting/setting_app.c', APP / 'src/system/app_registry.c',
                   APP / 'src/apps/user/user_app_config.c']
        sources.extend(APP / f'src/apps/{name}/{name}_app.c'
                       for name in ['bt', 'video', 'ota', 'user', 'hdmi_mcp', 'ai', 'pairing', 'iot', 'voice'])
        sources.append(APP / 'src/apps/ai/ai_free_chat_view.c')
        sources.append(APP / 'src/apps/iot/iot_client.c')
        sources.append(APP / 'src/apps/voice/voice_command.c')
        for directory in ['src/ui', 'src/ui/theme', 'src/ui/font', 'src/ui/images']:
            sources.extend(p for p in sorted((APP / directory).glob('*.c')) if p.name not in ('lockscreen.c', 'lockscreen_3d.c'))

        def compile_one(source):
            obj = build / (source.parent.name + '_' + source.stem + '.o')
            dep = obj.with_suffix('.d')
            dependencies = [source, config_path]
            if dep.exists():
                dependencies += [Path(p) for p in shlex.split(
                    dep.read_text().replace('\\\n', ' ').split(':', 1)[1])]
            if (not obj.exists() or not dep.exists() or any(
                    not p.exists() or p.stat().st_mtime > obj.stat().st_mtime
                    for p in dependencies)):
                run(['gcc', '-O1', '-g', '-ffunction-sections', '-fdata-sections',
                     f'-DLV_CONF_PATH="{config_path}"', f'-I{APP}', f'-I{APP / "lvgl"}',
                     '-DMIMI_ENABLE_VOICE_CONTROL=1', *capability_flags, '-MMD', '-MF', dep, '-c', source, '-o', obj], log)
            return obj

        with ThreadPoolExecutor(max_workers=4) as pool:
            objects = list(pool.map(compile_one, sources))
        run(['g++', '-Wl,--gc-sections', '-Wl,--wrap=fopen', '-Wl,--wrap=time', '-Wl,--wrap=mimi_iot_configuration_present', '-Wl,--wrap=connect', '-Wl,--wrap=mimi_iot_request', '-Wl,--wrap=lv_lottie_set_src_file', *objects,
             build / 'lvgl/lib/liblvgl.a', '-lm', '-lpthread', '-ljson-c', '-lcurl', '-o', build / 'review'], log)
        manager_fixture = build / 'review_manager_fixture.o'
        run(['gcc', '-O1', '-g', '-ffunction-sections', '-fdata-sections',
             '-DREVIEW_REAL_APP_MANAGER', '-DMIMI_ENABLE_VOICE_CONTROL=1', *capability_flags, f'-DLV_CONF_PATH="{config_path}"',
             f'-I{APP}', f'-I{APP / "lvgl"}', '-c', sources[0], '-o', manager_fixture], log)
        manager = compile_one(APP / 'src/system/app_manager.c')
        run(['g++', '-Wl,--gc-sections', '-Wl,--wrap=fopen', '-Wl,--wrap=time', '-Wl,--wrap=mimi_iot_configuration_present', '-Wl,--wrap=connect', '-Wl,--wrap=mimi_iot_request', '-Wl,--wrap=lv_lottie_set_src_file', manager_fixture,
             manager, *objects[1:], build / 'lvgl/lib/liblvgl.a', '-lm', '-lpthread', '-ljson-c', '-lcurl',
             '-o', build / 'review-manager'], log)
    from PIL import Image
    count = 0
    for width, height in resolutions:
        render_output = output if resolutions == [(1024, 768)] else output / f'{width}x{height}'
        render_output.mkdir(exist_ok=True)
        for page in args.pages:
            for language in ['zh', 'en']:
                for theme in ['light', 'dark']:
                    stem = f'{page}-{language}-{theme}'
                    ppm = render_output / (stem + '.ppm')
                    with (render_output / (stem + '.log')).open('w') as log:
                        run([build / 'review', page, language, theme, ppm, width, height], log)
                    with Image.open(ppm) as image:
                        assert image.size == (width, height)
                        image.save(render_output / (stem + '.png'))
                    ppm.unlink()
                    count += 1
        with (render_output / 'manager.log').open('w') as log:
            ppm = render_output / 'manager.ppm'
            run([build / 'review-manager', 'home', 'zh', 'dark', ppm, width, height], log)
            ppm.unlink()
    print(f'PASS: {count} LVGL renders ({", ".join(args.pages)}); page assertions and real app-manager checks passed.')


if __name__ == '__main__':
    main()
