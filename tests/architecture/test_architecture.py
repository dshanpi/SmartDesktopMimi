import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import re


ROOT = Path(__file__).resolve().parents[2]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


V2 = load_module("aitvbox_v2", ROOT / "core/contracts/v2/aitvbox_v2.py")
CLI = load_module("aitvbox_cli", ROOT / "tools/aitvbox.py")


class ProtocolV2Tests(unittest.TestCase):
    def request(self, **extra):
        value = {
            "schemaVersion": 2,
            "messageType": "request",
            "topic": "platform.describe",
            "requestId": "test-1",
            "payload": {},
        }
        value.update(extra)
        return value

    def test_round_trip_and_big_endian_length(self):
        frame = V2.encode(self.request())
        self.assertEqual(struct.unpack(">I", frame[:4])[0], len(frame) - 4)
        self.assertEqual(V2.Decoder().feed(frame), [self.request()])

    def test_fragmented_frame(self):
        frame = V2.encode(self.request())
        decoder = V2.Decoder()
        messages = []
        for byte in frame:
            messages.extend(decoder.feed(bytes((byte,))))
        self.assertEqual(messages, [self.request()])

    def test_coalesced_frames(self):
        first = self.request(requestId="one")
        second = self.request(requestId="two", futureField=True)
        self.assertEqual(V2.Decoder().feed(V2.encode(first) + V2.encode(second)), [first, second])

    def test_rejects_oversize_empty_invalid_json_and_version(self):
        with self.assertRaisesRegex(V2.ProtocolError, "64 KiB"):
            V2.Decoder().feed(struct.pack(">I", V2.MAX_FRAME_SIZE + 1))
        with self.assertRaises(V2.ProtocolError):
            V2.Decoder().feed(b"\0\0\0\0")
        with self.assertRaises(V2.ProtocolError):
            V2.Decoder().feed(struct.pack(">I", 1) + b"{")
        with self.assertRaises(V2.ProtocolError) as error:
            V2.encode(self.request(schemaVersion=3))
        self.assertEqual(error.exception.code, "UNSUPPORTED_VERSION")

    def test_request_id_event_sequence_and_payload_are_required(self):
        for change in (
            {"requestId": None},
            {"payload": []},
            {"messageType": "event", "requestId": None},
        ):
            with self.subTest(change=change), self.assertRaises(V2.ProtocolError):
                V2.encode(self.request(**change))
        event = self.request(messageType="event", requestId=None, sequence=0)
        self.assertEqual(V2.Decoder().feed(V2.encode(event))[0]["sequence"], 0)


class PlatformManifestTests(unittest.TestCase):
    PLATFORM_IDS = ("a133", "a527", "v883", "rk3576", "rv1106")

    def test_manifests_load_and_referenced_files_exist(self):
        for platform_id in self.PLATFORM_IDS:
            with self.subTest(platform=platform_id):
                manifest = CLI.load_platform(platform_id)
                self.assertEqual(manifest["id"], platform_id)
                self.assertTrue((ROOT / manifest["toolchain"]).is_file())
                self.assertTrue((ROOT / manifest["validation"]).is_file())
                self.assertIsInstance(manifest["capabilities"], list)
                self.assertIsInstance(manifest["hardware"], dict)
                self.assertTrue(manifest["hardware"])
                provider = (
                    ROOT / "apps/lv_port_linux/src/platform/a133/platform_capabilities_a133.c"
                    if platform_id == "a133"
                    else ROOT / "platforms" / platform_id / "platform_provider.c"
                )
                self.assertTrue(provider.is_file())

    def test_video_port_v2_has_explicit_frame_lifetime_and_encoder(self):
        ports = (ROOT / "core/ports/aitvbox_ports.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("AITVBOX_PLATFORM_ABI_VERSION 2U", ports)
        self.assertIn("aitvbox_video_frame_v2_t", ports)
        self.assertIn("acquire_frame", ports)
        self.assertIn("release_frame", ports)
        self.assertIn("aitvbox_video_encoder_port_t", ports)
        self.assertIn("request_key_frame", ports)

    def test_only_a133_claims_reference_support(self):
        self.assertEqual(CLI.load_platform("a133")["support"], "reference")
        for platform_id in self.PLATFORM_IDS[1:]:
            self.assertEqual(CLI.load_platform(platform_id)["support"], "skeleton")

    def test_skeleton_doctor_fails_closed(self):
        result = subprocess.run(
            [sys.executable, ROOT / "tools/aitvbox.py", "doctor", "--platform", "rk3576"],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("adapter skeleton", result.stderr)

    def test_a133_doctor_accepts_a_valid_sdk_shape(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory)
            for marker in CLI.load_platform("a133")["sdk"]["markers"]:
                path = sdk / marker
                if "." in path.name:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.touch()
                else:
                    path.mkdir(parents=True, exist_ok=True)
            errors = CLI.inspect(CLI.load_platform("a133"), sdk)
            self.assertEqual(errors, [])

    def test_a133_manifest_describes_release_hardware(self):
        hardware = CLI.load_platform("a133")["hardware"]
        self.assertEqual(hardware["board"], "B6")
        self.assertEqual(hardware["firmware_variant"], "UART0")
        self.assertIn("1024x768", hardware["local_display"])
        self.assertIn("1920x1080", hardware["hdmi_capture"])

    def test_one_click_release_wrapper_uses_canonical_release_pipeline(self):
        wrapper = (ROOT / "scripts/auto_build_package.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn("build_release.sh", wrapper)
        self.assertIn("--check-only", wrapper)
        self.assertIn("AITVBOX_A133_SDK", wrapper)
        self.assertIn("MISSING_COMMANDS", wrapper)
        self.assertIn("LICHEE_TOOLCHAIN_PATH", wrapper)
        self.assertIn("libaudio_subsys.a", wrapper)
        self.assertIn("至少需要 15 GiB", wrapper)
        self.assertNotIn("./build.sh pack", wrapper)

    def test_firmware_build_reaps_only_its_sdk_buildserver(self):
        firmware_build = (ROOT / "scripts/build_firmware.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn("SDK_BUILDSERVER_PIDS_BEFORE", firmware_build)
        self.assertIn("./buildserver --path ${SDK}", firmware_build)
        self.assertIn("stop_build_local_sdk_buildservers", firmware_build)
        self.assertIn("kill -TERM", firmware_build)
        self.assertIn("kill -KILL", firmware_build)
        self.assertLess(
            firmware_build.index("stop_build_local_sdk_buildservers\n"),
            firmware_build.index('if [[ -n "${ROOTFS_LIST}"'),
        )

    def test_architecture_dependency_gate(self):
        result = subprocess.run(
            [sys.executable, ROOT / "scripts/check_architecture.py"],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PASS", result.stdout)

    def test_architecture_gate_rejects_unicode_paths_in_code(self):
        gate = load_module(
            "architecture_unicode_path_gate",
            ROOT / "scripts/check_architecture.py",
        )
        bad_path = "factory-assistant-v1/02-" + "密钥" + "/100ask_ecdsa.pem"
        self.assertEqual(
            gate.unicode_path_tokens(f'root.join("{bad_path}")'),
            [bad_path],
        )
        self.assertEqual(
            gate.unicode_path_tokens('message = "设备缺少 /etc/100ask/secret，请重试"'),
            [],
        )

    def test_c_v2_codec_compiles_and_round_trips(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "ipc-v2-test"
            build = subprocess.run(
                [
                    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", ROOT / "apps/lv_port_linux/src/ipc",
                    ROOT / "tests/architecture/ipc_v2_test.c",
                    ROOT / "apps/lv_port_linux/src/ipc/ipc_v2.c",
                    "-ljson-c", "-o", binary,
                ],
                cwd=ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            self.assertEqual(build.returncode, 0, build.stderr)
            subprocess.run([binary], check=True)

    def test_public_release_audit_passes_and_reports_paths_not_secret_values(self):
        audit = load_module("public_release_audit", ROOT / "tools/public_release_audit.py")
        self.assertEqual(
            audit.path_rule(
                "third_party/100ask-iot-sdk/100ask_keys/100ask_ecdsa.pem"
            ),
            "private-or-restricted-path",
        )
        findings = audit.audit(
            ROOT, candidate=not (ROOT / ".gitmodules").exists(),
        )
        self.assertEqual(findings, [])
        self.assertTrue(all(set(item) == {"path", "rule"} for item in findings))

    def test_cloud_provider_defaults_to_stub_and_private_sdk_is_explicit(self):
        cmake = (ROOT / "apps/lv_port_linux/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        self.assertRegex(
            cmake,
            r'option\(AITVBOX_ENABLE_100ASK_CLOUD[\s\S]*?OFF\)',
        )
        self.assertIn("src/system/service_cloud_stub.c", cmake)
        self.assertIn("src/system/service_cloud_validation.c", cmake)
        self.assertIn("AITVBOX_100ASK_SDK_ROOT", cmake)
        self.assertNotIn("../../third_party/100ask-iot-sdk", cmake)

        build_script = (ROOT / "scripts/build_apps.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn('AITVBOX_ENABLE_100ASK_CLOUD:-OFF', build_script)
        self.assertIn('AITVBOX_100ASK_SDK_ROOT', build_script)
        self.assertIn('AITVBOX_100ASK_SDK_ROOT 必须位于产品源码树之外', build_script)

    def test_cloud_stub_compiles_without_private_sdk(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "service_cloud_stub.o"
            build = subprocess.run(
                [
                    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-c",
                    "-I", ROOT / "apps/lv_port_linux/src/system",
                    "-I", ROOT / "apps/lv_port_linux/src",
                    ROOT / "apps/lv_port_linux/src/system/service_cloud_stub.c",
                    "-o", output,
                ],
                cwd=ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            self.assertEqual(build.returncode, 0, build.stderr)

    def test_cloud_provider_rejects_missing_private_sdk(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                [
                    "cmake", "-S", ROOT / "apps/lv_port_linux",
                    "-B", directory,
                    "-DAITVBOX_ENABLE_100ASK_CLOUD=ON",
                    "-DAITVBOX_100ASK_SDK_ROOT=/definitely/missing/100ask-sdk",
                ],
                cwd=ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Private 100ask SDK is incomplete", result.stdout + result.stderr)

    def test_cloud_provider_rejects_private_sdk_inside_public_tree(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                [
                    "cmake", "-S", ROOT / "apps/lv_port_linux",
                    "-B", directory,
                    "-DAITVBOX_ENABLE_100ASK_CLOUD=ON",
                    f"-DAITVBOX_100ASK_SDK_ROOT={ROOT / 'third_party/100ask-iot-sdk'}",
                ],
                cwd=ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(
                "must be outside the product source tree",
                result.stdout + result.stderr,
            )

    def test_public_release_audit_scans_untracked_candidate_files(self):
        audit = load_module(
            "public_release_audit_untracked",
            ROOT / "tools/public_release_audit.py",
        )
        paths = {
            path.relative_to(ROOT).as_posix()
            for path in audit.tracked_files(ROOT)
        }
        self.assertIn("tools/public_release_audit.py", paths)
        self.assertIn("LICENSE", paths)

    def test_public_release_path_rules_distinguish_source_from_outputs(self):
        audit = load_module(
            "public_release_audit_path_rules",
            ROOT / "tools/public_release_audit.py",
        )
        self.assertIsNone(audit.path_rule(
            "third_party/TuyaOpen/src/libu8g2/u8g2/tools/font/build/Makefile"
        ))
        for relative in (
            "output/playwright/device-screen.png",
            "docs/debug-evidence/session/frame.raw.gz",
            "third_party/TuyaOpen/platform/LINUX/build/CMakeCache.txt",
            "third_party/TuyaOpen/.tools/python/bin/python3",
            "tools/factory-assistant/factory-core/target/debug/factory_cli",
            "web/node_modules/react/index.js",
            "third_party/100ask-iot-sdk/100ask_keys/device.pem",
            "vendor/tuyaopen-a133-b6-libs/MNN/libMNN.so",
        ):
            with self.subTest(path=relative):
                self.assertEqual(
                    audit.path_rule(relative),
                    "private-or-restricted-path",
                )
        self.assertEqual(
            audit.path_rule("a133-kvm-example/AI-TVBox.tar.gz"),
            "archive-or-release-image",
        )
        self.assertEqual(
            audit.path_rule("tools/factory-assistant/resources/adb.exe"),
            "unclassified-binary",
        )
        self.assertIsNone(audit.path_rule("apps/ipkvm/upstream/native.go"))
        self.assertEqual(
            audit.path_rule("apps/ipkvm/upstream/resource/netboot.xyz-multiarch.iso"),
            "archive-or-release-image",
        )
        self.assertEqual(
            audit.path_rule("third_party/upstream/libvendor.so.2.0.0"),
            "unclassified-binary",
        )
        self.assertEqual(
            audit.path_rule("web/public/fonts/vendor.woff2"),
            "unclassified-binary",
        )
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "opaque-executable"
            executable.write_bytes(b"\x7fELF" + b"\0" * 32)
            self.assertEqual(
                audit.file_magic_rule(executable),
                "unclassified-elf",
            )

    def test_public_release_profile_classifies_retained_binaries_and_exclusions(self):
        audit = load_module(
            "public_release_audit_profile",
            ROOT / "tools/public_release_audit.py",
        )
        findings, excluded, classified = audit.release_profile_findings(
            ROOT, candidate=not (ROOT / ".gitmodules").exists(),
        )
        self.assertEqual(findings, [])
        self.assertIn(
            "apps/ipkvm/upstream/resource/netboot.xyz-multiarch.iso",
            excluded,
        )
        self.assertIn("apps/ipkvm/upstream/resource/jetkvm_native", excluded)
        self.assertIn("apps/ipkvm/upstream/ui/public/fonts", excluded)
        self.assertIn(
            "apps/lv_port_linux/src/ui/font/SarasaUiSC-Regular.ttf",
            classified,
        )
        self.assertFalse(any(
            audit._under("apps/lv_port_linux/src/ui/font/SarasaUiSC-Regular.ttf", prefix)
            for prefix in excluded
        ))

    def test_pruned_public_lvgl_tree_disables_unused_demo_targets(self):
        cmake = (
            ROOT / "apps/lv_port_linux/CMakeLists.txt"
        ).read_text(encoding="utf-8")
        defaults = (
            ROOT / "apps/lv_port_linux/lv_conf.defaults"
        ).read_text(encoding="utf-8")
        main = (
            ROOT / "apps/lv_port_linux/src/main.c"
        ).read_text(encoding="utf-8")
        self.assertIn("set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL", cmake)
        self.assertIn("set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL", cmake)
        self.assertIn("LV_BUILD_DEMOS 0", defaults)
        self.assertIn("LV_BUILD_EXAMPLES 0", defaults)
        self.assertNotIn("lvgl/demos/lv_demos.h", main)

    def test_public_export_force_adds_the_audited_tree(self):
        exporter = (
            ROOT / "tools/export_public_source.py"
        ).read_text(encoding="utf-8")
        self.assertIn(
            'subprocess.run(["git", "add", "-f", "-A"]',
            exporter,
        )

    def test_public_asset_manifest_is_complete_and_content_addressed(self):
        audit = load_module(
            "public_release_audit_assets",
            ROOT / "tools/public_release_audit.py",
        )
        findings, classified = audit.asset_findings(ROOT)
        self.assertEqual(findings, [])
        profile_findings, excluded, _ = audit.release_profile_findings(
            ROOT, candidate=not (ROOT / ".gitmodules").exists(),
        )
        self.assertEqual(profile_findings, [])
        self.assertNotIn("R818_Backlight_v10.pdf", classified)
        self.assertIn(
            "docs/solution/assets/smart-desktop-mimi-hero.png",
            classified,
        )
        candidate_assets = {
            path.relative_to(ROOT).as_posix()
            for path in audit.tracked_files(ROOT)
            if path.is_file()
            and audit._is_documentation_asset(path.relative_to(ROOT).as_posix())
            and not any(
                audit._under(path.relative_to(ROOT).as_posix(), prefix)
                for prefix in excluded
            )
        }
        self.assertEqual(candidate_assets, classified)

    def test_public_tree_excludes_private_hardware_drawings(self):
        drawing_names = {
            "R818_Backlight_v10.pdf",
            "R818_Interface_V11.pdf",
            "R818_Mainboard_V10位号图.pdf",
            "R818_Mainboard_v10原理图.pdf",
            "R818_left_led_v10.pdf",
        }
        tracked = {
            path.relative_to(ROOT).as_posix()
            for path in ROOT.rglob("*")
            if path.is_file()
        }
        self.assertTrue(drawing_names.isdisjoint(tracked))

    def test_public_release_secret_patterns_are_high_confidence(self):
        audit = load_module(
            "public_release_audit_secrets",
            ROOT / "tools/public_release_audit.py",
        )
        samples = {
            "github-token": b"ghp_" + b"0123456789abcdefghijklmnop",
            "openai-api-key": b"sk-" + b"0123456789abcdefghijklmnop",
            "aws-access-key": b"AK" + b"IA0123456789ABCDEF",
            "tuya-device-credential": b"TUYA_OPENSDK_AUTHKEY=" + b"0123456789abcdef",
        }
        patterns = dict(audit.PRIVATE_TEXT)
        for rule, sample in samples.items():
            with self.subTest(rule=rule):
                self.assertIsNotNone(patterns[rule].search(sample))
        self.assertIsNone(
            patterns["openai-api-key"].search(b"aitvbox-browser-smoke-placeholder")
        )

    def test_public_scope_and_history_free_export_are_exposed(self):
        parsed = CLI.parser().parse_args([
            "test", "--platform", "a133", "--scope", "public",
        ])
        self.assertEqual(parsed.scope, "public")
        exporter = (ROOT / "tools/export_public_source.py").read_text(encoding="utf-8")
        self.assertIn("materialize_lvgl", exporter)
        self.assertIn("Initial public source release", exporter)
        self.assertIn("write_reproducible_archive", exporter)

    def test_sbom_generator_emits_both_formats(self):
        with tempfile.TemporaryDirectory() as directory:
            environment = os.environ.copy()
            environment["SOURCE_DATE_EPOCH"] = "1700000000"
            environment["SOURCE_REVISION"] = "1" * 40
            subprocess.run(
                [sys.executable, ROOT / "tools/generate_sbom.py", "--output", directory],
                cwd=ROOT,
                check=True,
                env=environment,
            )
            spdx = json.loads((Path(directory) / "aitvbox.spdx.json").read_text())
            cdx = json.loads((Path(directory) / "aitvbox.cdx.json").read_text())
            self.assertEqual(spdx["spdxVersion"], "SPDX-2.3")
            self.assertEqual(cdx["bomFormat"], "CycloneDX")
            self.assertEqual(spdx["creationInfo"]["created"], "2023-11-14T22:13:20Z")
            self.assertEqual(cdx["metadata"]["timestamp"], "2023-11-14T22:13:20Z")
            self.assertEqual(cdx["metadata"]["component"]["version"], "1" * 40)
            self.assertTrue(any(
                package["name"].startswith("Sarasa Gothic")
                for package in spdx["packages"]
            ))
            self.assertTrue(any(
                component.get("hashes", [{}])[0].get("content")
                == "9baf5d6c3d321a6438b9bad97dc5ca4e83cf1737d2efc989e2f8bdf2c662d357"
                for component in cdx["components"]
                if component.get("type") == "file"
            ))

            first_spdx = (Path(directory) / "aitvbox.spdx.json").read_bytes()
            first_cdx = (Path(directory) / "aitvbox.cdx.json").read_bytes()
            subprocess.run(
                [sys.executable, ROOT / "tools/generate_sbom.py", "--output", directory],
                cwd=ROOT,
                check=True,
                env=environment,
            )
            self.assertEqual(
                first_spdx,
                (Path(directory) / "aitvbox.spdx.json").read_bytes(),
            )
            self.assertEqual(
                first_cdx,
                (Path(directory) / "aitvbox.cdx.json").read_bytes(),
            )

    def test_document_links_and_governance_files_exist(self):
        for relative in (
            "LICENSE", "LICENSE-COMMERCIAL.md", "NOTICE", "SECURITY.md",
            "CONTRIBUTING.md", "CLA.md", "THIRD_PARTY_NOTICES.md",
            "README.md", "README.zh-CN.md", "docs/en/architecture.md",
            "docs/zh-CN/architecture.md", "docs/en/build-and-release.md",
            "docs/zh-CN/build-and-release.md", "docs/en/porting.md",
            "docs/zh-CN/porting.md", "docs/en/testing.md",
            "docs/zh-CN/testing.md", "docs/en/open-source-release.md",
            "docs/zh-CN/open-source-release.md", "docs/public-release.json",
            "apps/lv_port_linux/src/ui/font/LICENSE-Sarasa.txt",
        ):
            with self.subTest(path=relative):
                self.assertTrue((ROOT / relative).is_file())

    def test_a133_product_gate_authenticates_reviewed_sdk_gate(self):
        wrapper = (ROOT / "scripts/a133-bootchain-gate.sh").read_text(encoding="utf-8")
        match = re.search(r"REVIEWED_GATE_SHA256=([0-9a-f]{64})", wrapper)
        self.assertIsNotNone(match)
        sdk_gate = Path("/home/ubuntu/A133-Tina5.0-v0.9/scripts/a133-bootchain-gate.sh")
        if sdk_gate.is_file():
            import hashlib
            self.assertEqual(hashlib.sha256(sdk_gate.read_bytes()).hexdigest(), match.group(1))
        release = (ROOT / "scripts/build_release.sh").read_text(encoding="utf-8")
        self.assertIn('run_logged 50-bootchain-gate', release)
        self.assertLess(release.index('run_logged 50-bootchain-gate'),
                        release.index('echo "==> 归档发布产物"'))


if __name__ == "__main__":
    unittest.main()
