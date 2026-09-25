#!/usr/bin/env python3
"""Ubuntu integration tests for the trusted app and computer-control platform."""

import base64
import copy
import ctypes
import hashlib
import http.server
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import sys
import tarfile
import tempfile
import threading
import unittest


REPO = Path(__file__).resolve().parents[2]
AITAPP = REPO / "tools/aitapp/aitapp.py"
APPCTL = REPO / "platform/runtime/aitvbox-appctl"
AGENTCTL = REPO / "platform/runtime/aitvbox-agentctl"
ADMINCTL = REPO / "platform/runtime/aitvbox-adminctl"
TOGGLE_PRODUCT = REPO / "scripts/toggle_product.sh"
SYNC_TO_SDK = REPO / "scripts/sync_to_sdk.sh"
INJECT_KERNEL_CONFIG = REPO / "scripts/inject_platform_kernel_config.sh"
HIDCTL = REPO / "packaging/aitvbox-usb-hid/files/aitvbox-hidctl"
MCP_SOURCE = REPO / "apps/platform_services/src/mcpd.c"
AGENT_SOURCE = REPO / "apps/platform_services/src/agentd.c"
CONTROL_SOURCE = REPO / "apps/platform_services/src/controld.c"
APPD_SOURCE = REPO / "apps/platform_services/src/appd.c"
SAFE_IO_SOURCE = REPO / "apps/platform_services/src/safe_io.c"
APP_POLICY_SOURCE = REPO / "apps/platform_services/src/app_policy.c"
MCP_CLIENT = REPO / "tools/mcp/aitvbox_mcp_client.py"
EXAMPLE = REPO / "platform/app-sdk/examples/hello"
NATIVE_EXAMPLE = REPO / "platform/app-sdk/examples/native-status"
APP_SDK = REPO / "platform/app-sdk"
TINA_SDK = Path(os.environ.get(
    "AITVBOX_TINA_SDK", "/home/ubuntu/A133-Tina5.0-v0.9"
))
JPEG = b"\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00\xff\xd9"


def run(command, *, env=None, cwd=None, input_data=None, check=True):
    return subprocess.run(
        [str(item) for item in command],
        cwd=str(cwd or REPO),
        env=env,
        input=input_data,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=check,
    )


def write_executable(path, text):
    path.write_text(text, encoding="utf-8")
    path.chmod(0o755)


def macro(name, path):
    return '-D{}="{}"'.format(name, str(path))


class CaptureServer:
    def __init__(self, socket_path, image_path):
        self.socket_path = socket_path
        self.image_path = image_path
        self.error = None
        self.thread = threading.Thread(target=self._serve, daemon=True)

    def _serve(self):
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
                server.bind(str(self.socket_path))
                server.listen(1)
                client, _ = server.accept()
                with client:
                    request = client.recv(64)
                    if request != b"SNAPSHOT\n":
                        raise AssertionError("unexpected capture request")
                    self.image_path.write_bytes(JPEG)
                    client.sendall(
                        "OK {} {}\n".format(self.image_path, len(JPEG)).encode("ascii")
                    )
        except Exception as exc:  # surfaced by join()
            self.error = exc

    def __enter__(self):
        self.socket_path.unlink(missing_ok=True)
        self.thread.start()
        for _ in range(100):
            if self.socket_path.exists():
                return self
            threading.Event().wait(0.01)
        raise RuntimeError("capture socket did not start")

    def __exit__(self, exc_type, exc, traceback):
        self.thread.join(timeout=5)
        if self.thread.is_alive():
            raise RuntimeError("capture socket did not stop")
        self.socket_path.unlink(missing_ok=True)
        if self.error:
            raise self.error


class ModelServer:
    def __init__(self, cert, private_key, responses, expected_key):
        self.responses = list(responses)
        self.requests = []
        self.paths = []
        outer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                length = int(self.headers.get("Content-Length", "0"))
                request = json.loads(self.rfile.read(length))
                outer.requests.append((self.headers, request))
                outer.paths.append(self.path)
                if self.headers.get("Authorization") != "Bearer " + expected_key:
                    self.send_error(401)
                    return
                content = outer.responses.pop(0)
                body = json.dumps({
                    "choices": [{"message": {"content": content}}],
                }).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, _format, *_args):
                return

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(str(cert), str(private_key))
        self.server.socket = tls.wrap_socket(self.server.socket, server_side=True)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    @property
    def endpoint(self):
        return "https://localhost:{}/v1/chat/completions".format(
            self.server.server_address[1]
        )

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, exc_type, exc, traceback):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)


class PlatformHostTests(unittest.TestCase):
    def test_public_safe_io_is_durable_and_clears_sensitive_memory(self):
        source = SAFE_IO_SOURCE.read_text(encoding="utf-8")
        makefile = (
            REPO / "apps/platform_services/Makefile"
        ).read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            library_path = root / "libaitvbox-safe-io.so"
            result = run(
                [
                    "cc",
                    "-shared",
                    "-fPIC",
                    "-std=gnu99",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    SAFE_IO_SOURCE,
                    "-o",
                    library_path,
                ],
                check=False,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stderr.decode("utf-8", errors="replace"),
            )

            safe_io = ctypes.CDLL(str(library_path), use_errno=True)
            safe_io.aitvbox_atomic_write_file.argtypes = [
                ctypes.c_char_p,
                ctypes.c_char_p,
                ctypes.c_size_t,
                ctypes.c_uint,
            ]
            safe_io.aitvbox_atomic_write_file.restype = ctypes.c_int
            safe_io.aitvbox_durable_unlink.argtypes = [ctypes.c_char_p]
            safe_io.aitvbox_durable_unlink.restype = ctypes.c_int
            safe_io.aitvbox_secure_clear.argtypes = [
                ctypes.c_void_p,
                ctypes.c_size_t,
            ]
            safe_io.aitvbox_secure_clear.restype = None

            target = root / "value"
            target.write_text("old", encoding="ascii")
            target.chmod(0o644)
            payload = b"new durable value"
            self.assertEqual(
                safe_io.aitvbox_atomic_write_file(
                    os.fsencode(target), payload, len(payload), 0o600
                ),
                0,
            )
            self.assertEqual(target.read_bytes(), payload)
            self.assertEqual(target.stat().st_mode & 0o777, 0o600)
            self.assertFalse(any(root.glob(".aitvbox-tmp.*")))

            real_parent = root / "real-parent"
            real_parent.mkdir()
            linked_parent = root / "linked-parent"
            linked_parent.symlink_to(real_parent, target_is_directory=True)
            linked_target = linked_parent / "blocked"
            self.assertEqual(
                safe_io.aitvbox_atomic_write_file(
                    os.fsencode(linked_target), b"blocked", 7, 0o600
                ),
                -1,
            )
            self.assertFalse((real_parent / "blocked").exists())

            secret = ctypes.create_string_buffer(b"transient-secret")
            safe_io.aitvbox_secure_clear(secret, len(secret))
            self.assertEqual(secret.raw, b"\0" * len(secret))

            self.assertEqual(
                safe_io.aitvbox_durable_unlink(os.fsencode(target)), 0
            )
            self.assertFalse(target.exists())
            self.assertEqual(
                safe_io.aitvbox_durable_unlink(os.fsencode(target)), 0
            )

        self.assertIn("fsync(directory_fd)", source)
        self.assertIn("O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW", source)
        self.assertIn("SAFE_IO_SOURCE := src/safe_io.c", makefile)
        self.assertIn("src/controld.c $(SAFE_IO_SOURCE)", makefile)
        self.assertIn("src/appd.c $(SAFE_IO_SOURCE)", makefile)

    def test_cloud_and_ota_require_strict_tls_verification(self):
        cloud = (
            REPO / "apps/lv_port_linux/src/system/service_cloud.c"
        ).read_text(encoding="utf-8")
        ota = (
            REPO / "apps/lv_port_linux/src/system/service_ota.c"
        ).read_text(encoding="utf-8")
        package = (
            REPO / "packaging/aitvbox-suite/Makefile"
        ).read_text(encoding="utf-8")

        for source in (cloud, ota):
            self.assertNotIn("CURLOPT_SSL_VERIFYPEER, 0L", source)
            self.assertNotIn("CURLOPT_SSL_VERIFYHOST, 0L", source)
            self.assertIn("CURLOPT_SSL_VERIFYPEER, 1L", source)
            self.assertIn("CURLOPT_SSL_VERIFYHOST, 2L", source)
            self.assertIn("CURLOPT_PROTOCOLS", source)
            self.assertIn("CURLPROTO_HTTPS", source)
        self.assertIn("static bool configure_https_transport", cloud)
        self.assertEqual(cloud.count("configure_https_transport(curl, false)"), 2)
        self.assertIn("configure_https_transport(curl, true)", cloud)
        self.assertIn("CURLOPT_REDIR_PROTOCOLS,", cloud)
        self.assertIn("CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS", ota)
        self.assertIn("+ca-bundle", package)

    def test_cloud_accepts_only_current_six_digit_pairing_codes(self):
        cloud = (
            REPO / "apps/lv_port_linux/src/system/service_cloud.c"
        ).read_text(encoding="utf-8")
        validation = (
            REPO / "apps/lv_port_linux/src/system/service_cloud_validation.c"
        )

        with tempfile.TemporaryDirectory() as directory:
            library = Path(directory) / "libservice_cloud_validation.so"
            result = run(
                [
                    "cc",
                    "-shared",
                    "-fPIC",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    validation.parent,
                    validation,
                    "-o",
                    library,
                ],
                check=False,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stderr.decode("utf-8", errors="replace"),
            )

            validators = ctypes.CDLL(str(library))
            validators.service_cloud_bind_token_valid.argtypes = [ctypes.c_char_p]
            validators.service_cloud_bind_token_valid.restype = ctypes.c_bool
            validators.service_cloud_credential_value_valid.argtypes = [
                ctypes.c_char_p,
                ctypes.c_size_t,
                ctypes.c_size_t,
            ]
            validators.service_cloud_credential_value_valid.restype = ctypes.c_bool

            token_valid = validators.service_cloud_bind_token_valid
            for value in (b"000000", b"123456", b"999999"):
                self.assertTrue(token_valid(value), value)
            for value in (None, b"", b"12345", b"1234567", b"12a456", b"12 456"):
                self.assertFalse(token_valid(value), value)

            credential_valid = validators.service_cloud_credential_value_valid
            self.assertTrue(credential_valid(b"Abcdefghijklmnop-_.~", 16, 128))
            self.assertFalse(credential_valid(None, 16, 128))
            self.assertFalse(credential_valid(b"too-short", 16, 128))
            self.assertFalse(credential_valid(b"contains space here", 16, 128))
            self.assertFalse(credential_valid(b"contains/slash/value", 16, 128))
            self.assertFalse(credential_valid(b"validlengthvalue", 20, 10))

        self.assertIn("!service_cloud_bind_token_valid(token_out)", cloud)
        self.assertNotIn(
            "service_cloud_credential_value_valid(token_out", cloud
        )
        self.assertIn("secure_clear(local.device_secret", cloud)
        self.assertIn("local.generation == g_http_generation", cloud)
        self.assertIn("secure_clear(&completed, sizeof(completed))", cloud)

    def test_tuya_runtime_preflights_factory_license_before_spawn(self):
        source = (
            REPO / "apps/lv_port_linux/src/system/service_ai_runtime.c"
        ).read_text(encoding="utf-8")

        self.assertIn('TUYA_LICENSE_FILE_DEFAULT "/factory/tuya/license.env"', source)
        self.assertIn("file_stat.st_uid != 0", source)
        self.assertIn("S_IRWXG | S_IRWXO", source)
        self.assertIn('license_value_valid(value, 20)', source)
        self.assertIn('license_value_valid(value, 32)', source)
        ensure = source.index("static void ensure_runtime_processes(void)")
        preflight = source.index("!refresh_tuya_license_state()", ensure)
        spawn = source.index("start_tuya_process();", ensure)
        self.assertLess(preflight, spawn)
        self.assertIn("Tuya credentials are missing", source)

    def test_tuya_online_dp_sync_retries_after_every_mqtt_connect(self):
        source = (
            REPO / "third_party/TuyaOpen/apps/tuya.ai/your_chat_bot/src/tuya_main.c"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "DP_ONLINE_RETRY_DELAYS_MS[] = {1000, 2000, 5000, 10000, 30000}",
            source,
        )
        self.assertIn(
            "tal_sw_timer_create(ai_device_online_sync_timer_cb", source
        )
        self.assertIn("power_ret = ai_device_power_upload();", source)
        self.assertIn("volume_ret = ai_audio_volume_upload();", source)
        self.assertIn("DP online sync accepted:", source)
        self.assertIn("DP online sync failed:", source)

        connected = source.index("case TUYA_EVENT_MQTT_CONNECTED:")
        disconnected = source.index("case TUYA_EVENT_MQTT_DISCONNECT:")
        connected_block = source[connected:disconnected]
        self.assertIn("sg_dp_online_retry_index = 0;", connected_block)
        self.assertIn("ai_device_online_schedule();", connected_block)
        self.assertNotIn("static uint8_t first", connected_block)

        disconnect_block = source[disconnected:source.index(
            "case TUYA_EVENT_UPGRADE_NOTIFY:", disconnected
        )]
        self.assertIn("sg_mqtt_connected = FALSE;", disconnect_block)
        self.assertIn("tal_sw_timer_stop", disconnect_block)

        dp_source = (
            REPO / "third_party/TuyaOpen/src/tuya_cloud_service/schema/tuya_iot_dp.c"
        ).read_text(encoding="utf-8")
        self.assertIn('PR_INFO("DP cloud sync result: %d", result);', dp_source)

    def test_shipping_lvgl_ui_is_en_us(self):
        result = run([sys.executable, REPO / "scripts/audit_english_ui.py"])
        self.assertIn(b"English UI audit: PASS", result.stdout)

        tuya_config = (
            REPO / "integrations/tuyaopen/config/A133_B6.config"
        ).read_text(encoding="utf-8")
        self.assertIn("CONFIG_ENABLE_AI_LANGUAGE_ENGLISH=y", tuya_config)
        self.assertIn(
            "# CONFIG_ENABLE_AI_LANGUAGE_CHINESE is not set", tuya_config
        )

    def test_ipkvm_web_ui_defaults_to_chinese(self):
        store = (
            REPO / "apps/ipkvm/upstream/ui/src/hooks/stores.ts"
        ).read_text(encoding="utf-8")
        self.assertIn('language: "zh",', store)
        self.assertNotIn('language: "en",', store)

    def test_a133_atomic_compat_helper_cannot_recurse(self):
        source = (
            REPO / "third_party/TuyaOpen/platform/LINUX/tuyaos_adapter/src/"
            "tkl_audio/a133_abi_compat.cpp"
        ).read_text(encoding="utf-8")
        makefile = (
            REPO / "packaging/aitvbox-suite/Makefile"
        ).read_text(encoding="utf-8")

        helper = source[source.index("__aarch64_ldadd4_acq_rel"):]
        helper = helper[:helper.index("namespace std")]
        self.assertNotIn("return __atomic_fetch_add", helper)
        self.assertIn("ldaxr", helper)
        self.assertIn("stlxr", helper)
        self.assertIn("cbnz", helper)
        self.assertIn("$(VENDOR_LIBS)/MNN/libMNN.so", makefile)
        self.assertIn("$(VENDOR_LIBS)/MNN/libMNN_Express.so", makefile)

    def test_tuya_tts_stream_buffers_and_never_drops_partial_alsa_writes(self):
        datasink = (
            REPO / "third_party/TuyaOpen/src/audio_player/src/datasink/"
            "datasink_mem.c"
        ).read_text(encoding="utf-8")
        alsa = (
            REPO / "third_party/TuyaOpen/src/peripherals/audio_codecs/"
            "tdd_audio/src/tdd_audio_alsa.c"
        ).read_text(encoding="utf-8")
        config = (
            REPO / "integrations/tuyaopen/config/A133_B6.config"
        ).read_text(encoding="utf-8")

        self.assertIn("AI_PLAYER_MEM_PREBUFFER_SIZE", datasink)
        self.assertIn("ctx->buffering = true", datasink)
        self.assertIn("used < AI_PLAYER_MEM_PREBUFFER_SIZE", datasink)
        self.assertIn("player memory stream starved", datasink)
        self.assertIn("while (remaining > 0)", alsa)
        self.assertIn("snd_pcm_recover", alsa)
        self.assertIn("remaining -= (snd_pcm_uframes_t)written", alsa)
        self.assertIn("CONFIG_ALSA_BUFFER_FRAMES=4096", config)
        self.assertIn("CONFIG_ALSA_PERIOD_FRAMES=512", config)

    def test_linux_tuya_vad_reframes_alsa_periods_before_aec(self):
        vad = (
            REPO / "third_party/TuyaOpen/platform/LINUX/tuyaos_adapter/src/"
            "tkl_audio/tkl_vad.c"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "while (sg_mic_data_len >= AEC_VAD_FRAME_SIZE", vad
        )
        self.assertIn(
            "__tkl_aec_vad_process((int16_t *)sg_mic_data", vad
        )
        self.assertNotIn(
            "__tkl_aec_vad_process(mic_data, ref_data, out_data)", vad
        )
        self.assertIn(
            "rnn_vad_init(&timing_cfg, __s_rnn_vad_handle)", vad
        )
        self.assertIn(
            "float speech_min_ms;", vad
        )
        self.assertIn(
            "float noise_min_ms;", vad
        )
        self.assertIn(
            "timing_cfg.speech_min_ms = (float)config->speech_min_ms", vad
        )
        self.assertIn(
            "timing_cfg.noise_min_ms = (float)config->noise_min_ms", vad
        )
        self.assertIn("sg_mic_data_len = 0;", vad)
        self.assertIn("sg_ref_data_len = 0;", vad)
        self.assertIn("pthread_mutex_lock(&sg_vad_process_lock);", vad)
        self.assertIn("pthread_mutex_unlock(&sg_vad_process_lock);", vad)

    def test_persistent_data_binds_immediately_before_wifi(self):
        persistence = (
            REPO / "packaging/aitvbox-suite/files/aitvbox-data.init"
        ).read_text(encoding="utf-8")
        makefile = (
            REPO / "packaging/aitvbox-suite/Makefile"
        ).read_text(encoding="utf-8")

        self.assertIn("START=95", persistence)
        self.assertIn('wait_count=0', persistence)
        self.assertIn('wait_count=$((wait_count + 1))', persistence)
        self.assertIn("S95aitvbox-data", makefile)
        self.assertNotIn("S51aitvbox-data", makefile)

    def test_r818_ir_remote_is_wired_to_lvgl_keypad_navigation(self):
        launcher = (
            REPO / "packaging/aitvbox-suite/files/start-ui.sh"
        ).read_text(encoding="utf-8")
        backend = (
            REPO / "apps/lv_port_linux/src/lib/indev_backends/evdev.c"
        ).read_text(encoding="utf-8")
        evdev_patch = (
            REPO / "integrations/lvgl/0001-evdev-map-a133-remote-keys.patch"
        ).read_text(encoding="utf-8")

        self.assertIn("ir_remote_libdriver", launcher)
        self.assertIn("LV_LINUX_EVDEV_KEYPAD_DEVICE", launcher)
        self.assertIn("LV_INDEV_TYPE_KEYPAD", backend)
        self.assertIn("lv_indev_set_group(keypad, group)", backend)
        self.assertIn("app_manager_back_home();", backend)
        self.assertIn("case KEY_BACK:", evdev_patch)
        self.assertIn("case KEY_PLAYPAUSE:", evdev_patch)
        self.assertIn("case KEY_MENU:", evdev_patch)

    def test_hdmi_snapshot_providers_have_exclusive_socket_ownership(self):
        preview = (
            REPO / "apps/hdmi_preview/src/main.cpp"
        ).read_text(encoding="utf-8")
        kvm_video = (
            REPO / "apps/ipkvm/video/main.cpp"
        ).read_text(encoding="utf-8")

        for source in (preview, kvm_video):
            self.assertIn("capture-socket.lock", source)
            self.assertIn("flock(lock_fd, LOCK_EX | LOCK_NB)", source)
            self.assertIn("close(lock_fd);", source)

    def test_shared_capture_is_the_only_video_device_owner(self):
        capture = (
            REPO / "apps/ipkvm/video/main.cpp"
        ).read_text(encoding="utf-8")
        preview_service = (
            REPO / "apps/lv_port_linux/src/system/service_hdmi_preview.c"
        ).read_text(encoding="utf-8")
        product_init = (
            REPO / "packaging/aitvbox-suite/files/aitvbox.init"
        ).read_text(encoding="utf-8")
        ipkvm_init = (
            REPO / "packaging/aitvbox-ipkvm/files/aitvbox-ipkvm.init"
        ).read_text(encoding="utf-8")

        self.assertIn('memcmp(command, "DISPLAY 1\\n", 10)', capture)
        self.assertIn('memcmp(command, "STATUS\\n", 7)', capture)
        self.assertIn("present_local_frame();", capture)
        self.assertIn("g_stream_requested.load()", capture)
        self.assertIn("AITVBOX_SHARED_CAPTURE", preview_service)
        self.assertIn("shared_capture_set_display", preview_service)
        self.assertIn("AITVBOX_SHARED_CAPTURE=1", product_init)
        self.assertIn("AITVBOX_SHARED_CAPTURE=1", ipkvm_init)

    def test_shared_capture_prioritizes_the_low_latency_webrtc_branch(self):
        capture = (
            REPO / "apps/ipkvm/video/main.cpp"
        ).read_text(encoding="utf-8")
        webrtc = (
            REPO / "apps/ipkvm/upstream/webrtc.go"
        ).read_text(encoding="utf-8")
        desktop = (
            REPO / "apps/ipkvm/upstream/ui/src/layout/index.pc.tsx"
        ).read_text(encoding="utf-8")
        mobile = (
            REPO / "apps/ipkvm/upstream/ui/src/layout/index.mobile.tsx"
        ).read_text(encoding="utf-8")
        auth_recovery = (
            REPO / "apps/ipkvm/upstream/ui/src/authRecovery.ts"
        ).read_text(encoding="utf-8")

        encode_at = capture.index("g_encoder->encode(&packet)")
        snapshot_at = capture.index(
            "queue_snapshot_frame((const unsigned char*)gIonMem.vir);",
            encode_at,
        )
        display_at = capture.index("present_local_frame();", snapshot_at)
        self.assertLess(encode_at, snapshot_at)
        self.assertLess(snapshot_at, display_at)
        self.assertIn("const videoSampleQueueSize = 6", webrtc)
        self.assertIn("playoutDelayHint = 0", desktop)
        self.assertIn("playoutDelayHint = 0", mobile)
        for client in (desktop, mobile):
            self.assertIn(
                "reconnectAttempts: Number.MAX_SAFE_INTEGER",
                client,
            )
            self.assertNotIn(
                "!connectionFailed &&\n      isLegacySignalingEnabled.current",
                client,
            )
            self.assertIn(
                'console.log("Connection failed, restarting signaling")',
                client,
            )
            self.assertIn("getWebSocket()?.close();", client)
            self.assertIn("redirectToLoginIfSessionExpired", client)
            self.assertIn(
                "activePeerConnection !== expectedPeerConnection",
                client,
            )
            self.assertIn("peerConnectionRef.current !== pc", client)
            self.assertIn("cleanupAndStopReconnecting(pc)", client)

        self.assertIn('api.GET(`${DEVICE_API}/device`)', auth_recovery)
        self.assertIn("response.status !== 401", auth_recovery)
        self.assertIn('window.location.replace("/login-local")', auth_recovery)

        platform = (
            REPO / "apps/ipkvm/upstream/platform_a133.go"
        ).read_text(encoding="utf-8")
        web = (
            REPO / "apps/ipkvm/upstream/web.go"
        ).read_text(encoding="utf-8")
        self.assertNotIn(
            '// Browser session cookies must not survive a service restart.',
            platform,
        )
        self.assertIn(
            '"Failed to save authentication session"',
            web,
        )

    def test_new_devices_use_fifty_percent_standard_volume(self):
        settings = (
            REPO / "apps/lv_port_linux/src/system/settings.c"
        ).read_text(encoding="utf-8")
        self.assertIn(".volume = 50,", settings)

    def test_bluetooth_daemon_starts_before_product_backend(self):
        """BlueZ must be ready before lv_backend enters btmanager init."""
        init_text = (
            REPO / "packaging/aitvbox-suite/files/aitvbox-bluetooth.init"
        ).read_text(encoding="utf-8")
        makefile_text = (
            REPO / "packaging/aitvbox-suite/Makefile"
        ).read_text(encoding="utf-8")
        product_init = (
            REPO / "packaging/aitvbox-suite/files/aitvbox.init"
        ).read_text(encoding="utf-8")

        self.assertIn("START=97", init_text)
        self.assertIn("S97aitvbox-bluetooth", makefile_text)
        self.assertNotIn("S99aitvbox-bluetooth", makefile_text)
        self.assertIn("START=99", product_init)

    def test_btmanager_init_hook_is_idempotent(self):
        """The synchronous btmanager hook must not re-enter procd S96."""
        hook = (
            REPO / "packaging/aitvbox-suite/files/bt_init.sh"
        ).read_text(encoding="utf-8")

        self.assertIn('case "${1:-start}" in', hook)
        self.assertIn("if ! hci0_is_up; then", hook)
        self.assertIn("/etc/init.d/bluetooth_init worker", hook)
        self.assertNotIn("/etc/init.d/bluetooth_init start", hook)
        self.assertIn("stop)", hook)

    def test_deferred_wifi_driver_reopens_station_mode(self):
        """Cold boot must retry STA after the delayed AIC driver creates wlan0."""
        init_text = (
            REPO / "integrations/swupdate/files/bluetooth_init"
        ).read_text(encoding="utf-8")

        driver_pos = init_text.index("modprobe aic8800_fdrv")
        retry_pos = init_text.index("wifi -o sta")
        self.assertGreater(retry_pos, driver_pos)
        self.assertNotIn("wifi reload", init_text)

    def test_backend_reconciles_wifi_restored_after_deferred_probe(self):
        """A late system Wi-Fi connection must unblock AI/cloud services."""
        wifi_service = (
            REPO / "apps/lv_port_linux/src/system/service_wifi.c"
        ).read_text(encoding="utf-8")

        self.assertIn("reconcile_external_wifi_connection", wifi_service)
        self.assertIn("if (!ip_monitor_fetch(current_ip", wifi_service)
        self.assertIn("wifi_state.enabled = true;", wifi_service)
        self.assertIn("wifi_state.connected = true;", wifi_service)
        self.assertIn("send_wifi_runtime_status();", wifi_service)

    def test_wifi_scan_reinitializes_hal_after_deferred_driver_probe(self):
        wifi_service = (
            REPO / "apps/lv_port_linux/src/system/service_wifi.c"
        ).read_text(encoding="utf-8")

        self.assertIn("bool hal_ready;", wifi_service)
        self.assertIn("bool hal_init_pending;", wifi_service)
        self.assertIn("bool scan_pending;", wifi_service)
        self.assertIn(
            "network interface appeared; retrying Wi-Fi HAL init",
            wifi_service,
        )
        self.assertIn(
            "scan requested before HAL ready; initializing now",
            wifi_service,
        )
        self.assertIn("wifi_state.scan_pending = true", wifi_service)
        self.assertIn("if (still_scanning) send_wifi_scan_complete();", wifi_service)

    def test_product_packages_a2dp_sink_configuration(self):
        config_path = (
            REPO / "packaging/aitvbox-suite/files/bluetooth.json"
        )
        config = json.loads(config_path.read_text(encoding="utf-8"))
        makefile_text = (
            REPO / "packaging/aitvbox-suite/Makefile"
        ).read_text(encoding="utf-8")

        self.assertEqual(1, config["profile"]["a2dp_sink"])
        self.assertEqual(1, config["profile"]["avrcp"])
        self.assertEqual("default", config["a2dp_sink"]["device"])
        self.assertIn("./files/bluetooth.json", makefile_text)
        self.assertIn("/usr/share/aitvbox/bluetooth/bluetooth.json", makefile_text)
        self.assertIn("/usr/share/aitvbox/bluetooth/bt_init.sh", makefile_text)

        persistence = (
            REPO / "packaging/aitvbox-suite/files/aitvbox-data.init"
        ).read_text(encoding="utf-8")
        self.assertIn("seed_bluetooth_template bluetooth.json 0644", persistence)
        self.assertIn("seed_bluetooth_template bt_init.sh 0755", persistence)
        self.assertIn('[ -s "$dst" ] && return 0', persistence)

    def test_audio_boot_diagnostic_is_explicitly_opt_in(self):
        makefile_text = (
            REPO / "packaging/aitvbox-suite/Makefile"
        ).read_text(encoding="utf-8")
        diagnostic = (
            REPO / "tools/board/audio_chain_boot_test.init"
        ).read_text(encoding="utf-8")

        self.assertIn("AITVBOX_AUDIO_DIAGNOSTIC", makefile_text)
        self.assertNotIn("S99zz-aitvbox-audio-diagnostic", makefile_text)
        self.assertIn("audio-chain-test.wav", makefile_text)
        self.assertIn("already running, skip duplicate", diagnostic)
        self.assertIn("HDMI_PCM samples=", diagnostic)
        self.assertIn("hdmi_preview --service --display", diagnostic)
        self.assertIn("/etc/init.d/aitvbox stop", diagnostic)
        self.assertIn("/etc/init.d/aitvbox-ipkvm stop", diagnostic)

    def test_a2dp_sink_uses_explicit_bounded_pcm_playback(self):
        """Decoded PCM must have one ready consumer before callbacks start."""
        source = (
            REPO / "apps/lv_port_linux/src/system/service_bt.c"
        ).read_text(encoding="utf-8")
        player = (
            REPO / "apps/lv_port_linux/src/system/bt_pcm_player.c"
        ).read_text(encoding="utf-8")

        self.assertLess(source.index("bt_pcm_player_start()"),
                        source.index("bt_a2dp_sink_stream_cb_enable(true);"))
        self.assertIn("bt_pcm_player_push(channels, sampling, data, len);", source)
        self.assertIn("bt_a2dp_sink_stream_cb_enable(false);", source)
        self.assertIn('BT_PCM_DEVICE_DEFAULT "PlaybackDmix"', player)
        self.assertIn("BT_PCM_RING_BYTES", player)
        self.assertIn("snd_pcm_recover", player)
        self.assertIn("bt_pcm_player_set_active(false);", source)

    def test_backend_publishes_ipc_before_best_effort_hardware_settings(self):
        source = (
            REPO / "apps/lv_port_linux/src/backend_main.c"
        ).read_text(encoding="utf-8")

        self.assertLess(source.index("mw_init(true)"),
                        source.index("sys_backlight_apply_saved_setting()"))
        self.assertLess(source.index("mw_init(true)"),
                        source.index("sys_volume_apply_saved_setting()"))

    def test_backend_entrypoint_delegates_routing_and_service_lifecycle(self):
        source = (
            REPO / "apps/lv_port_linux/src/backend_main.c"
        ).read_text(encoding="utf-8")
        runtime = (
            REPO / "apps/lv_port_linux/src/backend/backend_runtime.c"
        ).read_text(encoding="utf-8")
        router = (
            REPO / "apps/lv_port_linux/src/backend/backend_command_router.c"
        ).read_text(encoding="utf-8")
        cmake = (
            REPO / "apps/lv_port_linux/CMakeLists.txt"
        ).read_text(encoding="utf-8")

        self.assertIn("backend_command_router_register()", source)
        self.assertIn("backend_runtime_start(&runtime_options)", source)
        self.assertIn("backend_runtime_tick()", source)
        self.assertIn("backend_runtime_stop()", source)
        self.assertNotIn('"system/service_wifi.h"', source)
        self.assertNotIn("handle_wifi_command", source)
        self.assertLess(runtime.index("service_led_init()"),
                        runtime.index("service_cloud_init()"))
        self.assertIn("{TOPIC_WIFI_COMMAND, handle_wifi_command}", router)
        self.assertIn("{TOPIC_CLOUD_COMMAND, handle_cloud_command}", router)
        self.assertIn('set(AITVBOX_PLATFORM "a133"', cmake)
        self.assertIn("AITVBOX_PLATFORM_SOURCES", cmake)

        platform_contract = (
            REPO / "apps/lv_port_linux/src/platform/platform_capabilities.h"
        ).read_text(encoding="utf-8")
        self.assertIn("AITVBOX_PLATFORM_ABI_VERSION", platform_contract)
        self.assertIn("aitvbox_platform_get_descriptor", platform_contract)

    def test_hdmi_i2s_passthrough_reports_signal_and_stability(self):
        source = (
            REPO / "apps/hdmi_preview/src/main.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn('HDMI_AUDIO_CAPTURE_DEFAULT "HDMIIn"', source)
        self.assertIn('HDMI_AUDIO_PLAYBACK_DEFAULT "PlaybackDmix"', source)
        self.assertIn("#define HDMI_AUDIO_CAPTURE_RATE 44100", source)
        self.assertIn("#define HDMI_AUDIO_PLAYBACK_RATE 44100", source)
        self.assertIn("rms_dbfs=%.1f", source)
        self.assertIn("peak_dbfs=%.1f", source)
        self.assertIn("capture_xruns=%u", source)
        self.assertIn("playback_xruns=%u", source)

        settings = (
            REPO / "apps/lv_port_linux/src/system/settings.c"
        ).read_text(encoding="utf-8")
        self.assertIn(".hdmi_enabled = true", settings)

    @classmethod
    def setUpClass(cls):
        cls.workspace = Path(tempfile.mkdtemp(prefix="aitvbox-host-tests-"))
        cls.bin_dir = cls.workspace / "bin"
        cls.bin_dir.mkdir()
        cls.runtime = cls.workspace / "runtime"
        cls.runtime.mkdir()
        cls.capture_socket = cls.runtime / "capture.sock"
        cls.screen = cls.runtime / "screen.jpg"
        cls.hid_log = cls.runtime / "hid.log"
        cls.keyboard = cls.runtime / "hidg0"
        cls.pointer = cls.runtime / "hidg1"
        cls.gpio = cls.runtime / "gpio"
        cls.i2c = cls.runtime / "i2c"
        cls.spi = cls.runtime / "spi"
        cls.uart = cls.runtime / "uart"
        cls.usb_host = cls.runtime / "usb"
        cls.agent_lock = cls.runtime / "agent.lock"
        cls.stop_file = cls.runtime / "agent.stop"
        cls.keyboard.touch()
        cls.pointer.touch()
        cls.uart.touch()
        cls.usb_host.mkdir()

        cls.mock_hid = cls.bin_dir / "aitvbox-hidctl"
        write_executable(
            cls.mock_hid,
            "#!/bin/sh\nprintf '%s\\n' \"$*\" >>'{}'\n".format(cls.hid_log),
        )
        cls.mock_capture = cls.bin_dir / "hdmi_preview"
        write_executable(
            cls.mock_capture,
            "#!/bin/sh\n[ \"$1\" = --snapshot ]\ncp '{}' '{}'\n".format(
                cls.runtime / "fixture.jpg", cls.screen
            ),
        )
        (cls.runtime / "fixture.jpg").write_bytes(JPEG)

        cls.mcpd = cls.bin_dir / "aitvbox-mcpd"
        run([
            "gcc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror",
            macro("CAPTURE_SOCKET", cls.capture_socket),
            macro("CAPTURE_PATH", cls.screen),
            macro("HIDCTL_PATH", cls.mock_hid),
            macro("HID_KEYBOARD_PATH", cls.keyboard),
            macro("HID_POINTER_PATH", cls.pointer),
            macro("GPIO_PATH", cls.gpio),
            macro("I2C_PATH", cls.i2c),
            macro("SPI_PATH", cls.spi),
            macro("UART_PATH", cls.uart),
            macro("USB_HOST_PATH", cls.usb_host),
            macro("AGENT_LOCK_FILE", cls.agent_lock),
            macro("AGENT_STOP_FILE", cls.stop_file),
            MCP_SOURCE, "-o", cls.mcpd, "-ljson-c",
        ])
        cls.app_policy = cls.bin_dir / "aitvbox-app-policy"
        run([
            "gcc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror",
            APP_POLICY_SOURCE, "-o", cls.app_policy, "-ljson-c",
        ])

        cls.app_socket = cls.runtime / "apps.sock"
        cls.native_root = cls.runtime / "native-apps"
        cls.native_app = cls.native_root / "com.100ask.native-status"
        (cls.native_app / "bin").mkdir(parents=True)
        shutil.copy2(
            NATIVE_EXAMPLE / "manifest.json",
            cls.native_app / "manifest.json",
        )
        native_manifest = json.loads(
            (cls.native_app / "manifest.json").read_text(encoding="utf-8")
        )
        native_manifest["permissions"] = ["storage.app"]
        (cls.native_app / "manifest.json").write_text(
            json.dumps(native_manifest), encoding="utf-8"
        )
        (cls.native_app / "manifest.json").chmod(0o644)
        (cls.native_app / "data").mkdir()
        (cls.native_app / "data").chmod(0o700)
        (cls.native_app / ".aitvbox-publisher-key.sha256").write_text(
            "0" * 64 + "\n", encoding="ascii"
        )
        (cls.native_app / ".aitvbox-publisher-key.sha256").chmod(0o444)
        cls.native_test_source = cls.runtime / "native-test-main.c"
        cls.native_test_source.write_text(
            """#include "aitvbox/app.h"
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

struct test_open_how {
    uint64_t flags;
    uint64_t mode;
    uint64_t resolve;
};

int main(int argc, char **argv)
{
    const char *action = aitvbox_app_action(argc, argv);
    if (!action)
        return 2;
    if (!strcmp(action, "status")) {
        char result[2048];
        if (!aitvbox_capability_call(
                "hardware.capabilities", "{}", result, sizeof(result)))
            return aitvbox_app_reply(true, "Hardware status is available.");
        return aitvbox_app_reply(false, "Hardware status is unavailable.");
    }
    if (!strcmp(action, "sandbox")) {
        int network = socket(AF_INET, SOCK_STREAM, 0);
        int file = open("/tmp/aitvbox-app-bypass", O_WRONLY | O_CREAT, 0600);
        int directory = mkdir("/tmp/aitvbox-app-bypass-dir", 0700);
        int openat2_file = -1;
#ifdef SYS_openat2
        struct test_open_how how = {
            .flags = O_WRONLY | O_CREAT,
            .mode = 0600,
        };
        openat2_file = syscall(
            SYS_openat2, AT_FDCWD, "/tmp/aitvbox-app-openat2-bypass",
            &how, sizeof(how));
#endif
        if (network >= 0)
            close(network);
        if (file >= 0)
            close(file);
        if (openat2_file >= 0)
            close(openat2_file);
        return aitvbox_app_reply(
            network < 0 && file < 0 && directory < 0 && openat2_file < 0,
            "Sandbox bypass was blocked.");
    }
    if (!strcmp(action, "storage")) {
        char value[64];
        bool found = false;
        if (aitvbox_storage_write("state", "score=2048") ||
            aitvbox_storage_read("state", value, sizeof(value), &found))
            return aitvbox_app_reply(false, "Storage request failed.");
        return aitvbox_app_reply(
            found && !strcmp(value, "score=2048"),
            found ? value : "Storage value was not found.");
    }
    return aitvbox_app_reply(false, "Unknown action.");
}
""",
            encoding="ascii",
        )
        run([
            "gcc", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-I", APP_SDK / "include",
            cls.native_test_source, APP_SDK / "src/app.c",
            "-o", cls.native_app / "bin/app", "-ljson-c",
        ])
        (cls.native_app / "bin/app").chmod(0o555)
        (cls.native_app / "bin").chmod(0o555)
        cls.native_app.chmod(0o555)
        cls.native_root.chmod(0o755)
        cls.declarative_app = cls.native_root / "com.100ask.hello"
        cls.declarative_app.mkdir()
        shutil.copy2(
            EXAMPLE / "manifest.json",
            cls.declarative_app / "manifest.json",
        )
        (cls.declarative_app / "manifest.json").chmod(0o644)
        (cls.declarative_app / ".aitvbox-publisher-key.sha256").write_text(
            "0" * 64 + "\n", encoding="ascii"
        )
        (cls.declarative_app / ".aitvbox-publisher-key.sha256").chmod(0o444)
        cls.declarative_app.chmod(0o555)
        cls.appd = cls.bin_dir / "aitvbox-appd"
        user_id = os.geteuid()
        group_id = os.getegid()
        run([
            "gcc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror",
            macro("APP_SOCKET", cls.app_socket),
            macro("APP_ROOT", cls.native_root),
            macro("MCP_PATH", cls.mcpd),
            "-DAPP_UID={}".format(user_id),
            "-DAPP_GID={}".format(group_id),
            "-DAPP_OWNER_UID={}".format(user_id),
            "-DAPP_MAX_WORKERS=2",
            "-DAPP_CLIENT_TIMEOUT_MS=500",
            APPD_SOURCE, SAFE_IO_SOURCE, "-o", cls.appd, "-ljson-c",
        ])

        cls.secret_dir = cls.runtime / "secrets"
        cls.secret_dir.mkdir()
        cls.key_file = cls.secret_dir / "model-api-key"
        cls.config_file = cls.secret_dir / "model.conf"
        cls.tls_key = cls.runtime / "tls.key"
        cls.tls_cert = cls.runtime / "tls.pem"
        cls.agentd = cls.bin_dir / "aitvbox-agentd"
        run([
            "gcc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror",
            macro("KEY_FILE", cls.key_file),
            macro("CONFIG_FILE", cls.config_file),
            macro("SCREEN_FILE", cls.screen),
            macro("STOP_FILE", cls.stop_file),
            macro("AGENT_LOCK_FILE", cls.agent_lock),
            macro("HDMI_PREVIEW_PATH", cls.mock_capture),
            macro("HIDCTL_PATH", cls.mock_hid),
            macro("HID_KEYBOARD_PATH", cls.keyboard),
            macro("HID_POINTER_PATH", cls.pointer),
            macro("HID_LOCK_DIR", cls.runtime / "agent-hid.lock"),
            macro("CAINFO_PATH", cls.tls_cert),
            "-DKEYBOARD_HOLD_US=1000",
            "-DNETEASE_STEP_GAP_US=1000",
            "-DNETEASE_SETTLE_US=1000",
            AGENT_SOURCE, "-o", cls.agentd, "-ljson-c", "-lcurl",
        ])

        cls.control_socket = cls.runtime / "control.sock"
        cls.control_key = cls.secret_dir / "control-model-api-key"
        cls.control_config = cls.secret_dir / "control-model.conf"
        cls.control_stop = cls.runtime / "control-agent.stop"
        cls.control_lock = cls.runtime / "control-agent.lock"
        cls.control_log = cls.runtime / "control-agent.log"
        cls.mock_agent = cls.bin_dir / "mock-agentd"
        write_executable(
            cls.mock_agent,
            """#!/bin/sh
set -eu
[ "${{1:-}}" = run ]
printf '%s\n' "$$" >'{lock}'
cleanup() {{
    rm -f '{lock}'
}}
trap 'cleanup; exit 0' TERM INT
if [ "${{2:-}}" = "hold task" ]; then
    while [ ! -e '{stop}' ]; do
        sleep 0.02
    done
    printf 'STOPPED\n'
else
    printf 'DONE %s\n' "${{2:-}}"
fi
cleanup
""".format(lock=cls.control_lock, stop=cls.control_stop),
        )
        cls.controld = cls.bin_dir / "aitvbox-controld"
        run([
            "gcc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror",
            macro("CONTROL_SOCKET", cls.control_socket),
            macro("KEY_FILE", cls.control_key),
            macro("CONFIG_FILE", cls.control_config),
            macro("STOP_FILE", cls.control_stop),
            macro("AGENT_LOCK_FILE", cls.control_lock),
            macro("AGENT_PATH", cls.mock_agent),
            macro("HIDCTL_PATH", cls.mock_hid),
            macro("AGENT_LOG", cls.control_log),
            CONTROL_SOURCE, SAFE_IO_SOURCE, "-o", cls.controld, "-ljson-c",
        ])

        run([
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-days", "1", "-subj", "/CN=localhost",
            "-addext", "subjectAltName=DNS:localhost",
            "-keyout", cls.tls_key, "-out", cls.tls_cert,
        ])

        cls.signing_key = cls.runtime / "developer.key"
        cls.public_key = cls.runtime / "developer.pem"
        cls.other_key = cls.runtime / "other.key"
        cls.other_public_key = cls.runtime / "other.pem"
        run([
            "openssl", "genpkey", "-algorithm", "RSA",
            "-pkeyopt", "rsa_keygen_bits:2048", "-out", cls.signing_key,
        ])
        run([
            "openssl", "pkey", "-in", cls.signing_key,
            "-pubout", "-out", cls.public_key,
        ])
        run([
            "openssl", "genpkey", "-algorithm", "RSA",
            "-pkeyopt", "rsa_keygen_bits:2048", "-out", cls.other_key,
        ])
        run([
            "openssl", "pkey", "-in", cls.other_key,
            "-pubout", "-out", cls.other_public_key,
        ])

        cls.jsonfilter = cls.bin_dir / "jsonfilter"
        write_executable(
            cls.jsonfilter,
            """#!/usr/bin/env python3
import json
import sys
source = None
expressions = []
args = iter(sys.argv[1:])
for arg in args:
    if arg == "-i":
        source = next(args)
    elif arg == "-e":
        expressions.append(next(args))
with open(source, encoding="utf-8") as stream:
    value = json.load(stream)
for expression in expressions:
    current = value
    path = (expression[2:] if expression.startswith("@.") else expression).split(".")
    expand = path[-1].endswith("[*]")
    if expand:
        path[-1] = path[-1][:-3]
    for part in path:
        current = current[part]
    if expand:
        for item in current:
            print(item)
    elif isinstance(current, bool):
        print("true" if current else "false")
    else:
        print(current)
            """,
        )
        write_executable(
            cls.bin_dir / "id",
            "#!/bin/sh\n[ \"${1:-}\" = -u ] && { echo 0; exit 0; }\n"
            "exec /usr/bin/id \"$@\"\n",
        )
        cls.adb_private_key = cls.runtime / "adbkey"
        run(["adb", "keygen", cls.adb_private_key])

    @classmethod
    def tearDownClass(cls):
        for root, directories, files in os.walk(cls.workspace):
            os.chmod(root, 0o700)
            for name in directories:
                os.chmod(Path(root) / name, 0o700)
            for name in files:
                os.chmod(Path(root) / name, 0o600)
        shutil.rmtree(cls.workspace)

    def setUp(self):
        Path("/tmp/aitvbox-app-bypass").unlink(missing_ok=True)
        self.hid_log.write_text("", encoding="utf-8")
        self.keyboard.write_bytes(b"")
        self.stop_file.unlink(missing_ok=True)
        self.agent_lock.unlink(missing_ok=True)
        for path in (
            self.control_socket, self.control_key, self.control_config,
            self.control_stop, self.control_lock, self.control_log,
            self.app_socket,
        ):
            path.unlink(missing_ok=True)

    def mcp(self, requests):
        payload = b"".join(
            json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\n"
            for request in requests
        )
        result = run([self.mcpd], input_data=payload)
        return [json.loads(line) for line in result.stdout.splitlines()]

    def control(self, request):
        payload = json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\n"
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.settimeout(2)
            client.connect(str(self.control_socket))
            client.sendall(payload)
            response = b""
            while not response.endswith(b"\n"):
                block = client.recv(4096)
                if not block:
                    break
                response += block
        return json.loads(response)

    def app_service(self, request):
        payload = json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\n"
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.settimeout(8)
            client.connect(str(self.app_socket))
            client.sendall(payload)
            response = b""
            while not response.endswith(b"\n"):
                block = client.recv(1024 * 1024)
                if not block:
                    break
                response += block
        return json.loads(response)

    def test_native_app_runtime_uses_permission_broker(self):
        daemon = subprocess.Popen(
            [str(self.appd)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            for _ in range(200):
                if self.app_socket.exists():
                    break
                if daemon.poll() is not None:
                    self.fail("appd exited: " + daemon.stderr.read().decode())
                threading.Event().wait(0.01)
            else:
                self.fail("application socket did not start")

            result = self.app_service({
                "command": "invoke",
                "appId": "com.100ask.native-status",
                "action": "status",
            })
            self.assertTrue(result["ok"], result)
            self.assertEqual(result["message"], "Hardware status is available.")

            stored = self.app_service({
                "command": "invoke",
                "appId": "com.100ask.native-status",
                "action": "storage",
            })
            self.assertTrue(stored["ok"], stored)
            self.assertEqual(stored["message"], "score=2048")
            storage_value = self.native_app / "data/state.value"
            self.assertEqual(storage_value.read_text(encoding="utf-8"), "score=2048")
            self.assertEqual(storage_value.stat().st_mode & 0o777, 0o600)
            self.assertFalse(any(
                self.native_app.joinpath("data").glob(".aitvbox-tmp.*")
            ))

            with CaptureServer(self.capture_socket, self.screen):
                platform_action = self.app_service({
                    "command": "platformAction",
                    "appId": "com.100ask.hello",
                    "capability": "capture.snapshot",
                    "arguments": {},
                })
            self.assertTrue(platform_action["ok"], platform_action)
            self.assertNotIn("result", platform_action)
            self.assertEqual(platform_action["message"], "capability completed")

            sandboxed = self.app_service({
                "command": "invoke",
                "appId": "com.100ask.native-status",
                "action": "sandbox",
            })
            self.assertTrue(sandboxed["ok"], sandboxed)
            self.assertFalse(Path("/tmp/aitvbox-app-bypass").exists())

            direct = run(
                [self.native_app / "bin/app", "--action", "status"],
                env={**os.environ, "AITVBOX_APP_SOCKET": str(self.app_socket)},
                check=False,
            )
            self.assertEqual(direct.returncode, 0, direct.stderr.decode())
            self.assertFalse(json.loads(direct.stdout)["ok"])

            denied = self.app_service({
                "command": "capability",
                "capability": "hardware.capabilities",
                "arguments": {},
            })
            self.assertFalse(denied["ok"])
            self.assertIn("installed native application", denied["message"])

            invalid = self.app_service({
                "command": "invoke",
                "appId": "../../etc",
                "action": "status",
            })
            self.assertFalse(invalid["ok"])

            marker = self.native_app / ".aitvbox-publisher-key.sha256"
            marker.chmod(0o644)
            marker.write_text("not-a-fingerprint\n", encoding="ascii")
            marker.chmod(0o444)
            invalid_marker = self.app_service({
                "command": "invoke",
                "appId": "com.100ask.native-status",
                "action": "status",
            })
            self.assertFalse(invalid_marker["ok"])
            marker.chmod(0o644)
            marker.write_text("0" * 64 + "\n", encoding="ascii")
            marker.chmod(0o444)

            original_mode = self.native_app.stat().st_mode & 0o777
            self.native_app.chmod(original_mode | 0o022)
            writable_directory = self.app_service({
                "command": "invoke",
                "appId": "com.100ask.native-status",
                "action": "status",
            })
            self.assertFalse(writable_directory["ok"])
            self.native_app.chmod(original_mode)

            blockers = []
            for _ in range(2):
                blocker = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                blocker.settimeout(2)
                blocker.connect(str(self.app_socket))
                blockers.append(blocker)
                threading.Event().wait(0.05)
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                client.settimeout(2)
                client.connect(str(self.app_socket))
                busy = json.loads(client.recv(4096))
            self.assertFalse(busy["ok"])
            self.assertIn("busy", busy["message"])
            for blocker in blockers:
                blocker.close()
        finally:
            daemon.terminate()
            try:
                daemon.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                daemon.kill()
                daemon.communicate(timeout=5)

    def wait_control_state(self, expected):
        for _ in range(200):
            status = self.control({"command": "status"})
            if status["state"] == expected:
                return status
            threading.Event().wait(0.01)
        self.fail("control state did not become {}".format(expected))

    def test_control_daemon_key_task_stop_and_protocol(self):
        daemon = subprocess.Popen(
            [str(self.controld)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            for _ in range(200):
                if self.control_socket.exists():
                    break
                if daemon.poll() is not None:
                    self.fail("controld exited: " + daemon.stderr.read().decode())
                threading.Event().wait(0.01)
            else:
                self.fail("control socket did not start")

            status = self.control({"command": "status"})
            self.assertEqual(status["state"], "idle")
            self.assertFalse(status["configured"])

            invalid = self.control({"command": "setKey", "key": "not-a-key"})
            self.assertFalse(invalid["ok"])
            strict = self.control({"command": "status", "unexpected": True})
            self.assertFalse(strict["ok"])

            configured = self.control({
                "command": "setKey", "key": "ark-control-testing-123",
            })
            self.assertTrue(configured["ok"])
            invalid_provider = self.control({
                "command": "configure",
                "endpoint": "http://insecure.invalid/v1",
                "model": "vision-test",
            })
            self.assertFalse(invalid_provider["ok"])
            provider = self.control({
                "command": "configure",
                "endpoint": "https://provider.invalid/v1/chat/completions",
                "model": "vision-test",
            })
            self.assertTrue(provider["ok"])
            status = self.control({"command": "status"})
            self.assertEqual(
                status["endpoint"],
                "https://provider.invalid/v1/chat/completions",
            )
            self.assertEqual(status["model"], "vision-test")
            self.assertEqual(self.control_key.stat().st_mode & 0o777, 0o600)
            self.assertEqual(self.control_config.stat().st_mode & 0o777, 0o600)
            self.assertNotIn(
                "ark-control-testing-123",
                self.control_log.read_text(encoding="utf-8")
                if self.control_log.exists() else "",
            )

            embedding = self.control({
                "command": "configure",
                "endpoint": "https://provider.invalid/v3",
                "model": "doubao-embedding-vision",
            })
            self.assertFalse(embedding["ok"])
            self.assertIn("embedding-only", embedding["message"])
            rejected_status = self.control({"command": "status"})
            self.assertEqual(rejected_status["state"], "idle")
            self.assertEqual(rejected_status["model"], "vision-test")

            started = self.control({
                "command": "start", "task": "quick task", "maxSteps": 3,
            })
            self.assertTrue(started["ok"])
            completed = self.wait_control_state("succeeded")
            self.assertEqual(completed["detail"], "quick task")

            started = self.control({
                "command": "start", "task": "hold task", "maxSteps": 30,
            })
            self.assertTrue(started["ok"])
            self.wait_control_state("running")
            duplicate = self.control({
                "command": "start", "task": "second task", "maxSteps": 1,
            })
            self.assertFalse(duplicate["ok"])
            stopped = self.control({"command": "stop"})
            self.assertTrue(stopped["ok"])
            self.wait_control_state("idle")
            self.assertTrue(self.control_stop.exists())
            self.assertEqual(
                self.hid_log.read_text(encoding="utf-8").splitlines(),
                ["key 0 0", "mouse 0 0 0 0"],
            )

            cleared = self.control({"command": "clearKey"})
            self.assertTrue(cleared["ok"])
            self.assertFalse(self.control_key.exists())
            self.assertFalse(any(self.secret_dir.glob(".aitvbox-tmp.*")))
        finally:
            daemon.terminate()
            try:
                daemon.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                daemon.kill()
                daemon.communicate(timeout=5)

    def test_mcp_stdio_capture_hid_and_boundaries(self):
        mcp_source = MCP_SOURCE.read_text(encoding="utf-8")
        self.assertIn('#define HID_KEYBOARD_PATH "/dev/hidg0"', mcp_source)
        self.assertIn('#define HID_POINTER_PATH "/dev/hidg1"', mcp_source)
        self.assertNotIn('#define HID_POINTER_PATH "/dev/hidg0"', mcp_source)

        requests = [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize",
             "params": {
                 "protocolVersion": "2025-11-25",
                 "capabilities": {},
                 "clientInfo": {"name": "host-test", "version": "1.0"},
             }},
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
            {"jsonrpc": "2.0", "id": 3, "method": "tools/call",
             "params": {"name": "hardware.capabilities", "arguments": {}}},
            {"jsonrpc": "2.0", "id": 4, "method": "tools/call",
             "params": {"name": "computer.key",
                        "arguments": {"keycode": 4, "modifier": 2}}},
            {"jsonrpc": "2.0", "id": 5, "method": "tools/call",
             "params": {"name": "computer.mouse",
                        "arguments": {"dx": -5, "dy": 7, "buttons": 1, "wheel": -1}}},
            {"jsonrpc": "2.0", "id": 6, "method": "tools/call",
             "params": {"name": "computer.key", "arguments": {"keycode": 102}}},
            {"jsonrpc": "2.0", "id": 7, "method": "tools/call",
             "params": {"name": "computer.mouse", "arguments": {"dx": 128, "dy": 0}}},
            {"jsonrpc": "2.0", "id": 8, "method": "tools/call",
             "params": {"name": "computer.capture", "arguments": {}}},
            {"jsonrpc": "2.0", "id": 9, "method": "tools/call",
             "params": {"name": "safety.stop", "arguments": {}}},
            {"jsonrpc": "2.0", "id": 10, "method": "missing"},
            {"jsonrpc": "2.0", "id": 11, "method": "tools/call",
             "params": {"name": "computer.key",
                        "arguments": {"keycode": 4, "unexpected": 1}}},
        ]
        with CaptureServer(self.capture_socket, self.screen):
            responses = self.mcp(requests)
        by_id = {item["id"]: item for item in responses}
        self.assertEqual(by_id[1]["result"]["protocolVersion"], "2025-11-25")
        self.assertEqual(len(by_id[2]["result"]["tools"]), 5)
        capabilities = json.loads(
            by_id[3]["result"]["content"][0]["text"]
        )
        self.assertEqual(
            capabilities,
            {
                "capture": True, "keyboard": True, "pointer": True,
                "gpio": False, "i2c": False, "spi": False, "uart": True,
                "usbHost": True,
            },
        )

        self.pointer.unlink()
        try:
            missing_pointer = self.mcp([
                {"jsonrpc": "2.0", "id": 1, "method": "initialize",
                 "params": {"protocolVersion": "2025-11-25"}},
                {"jsonrpc": "2.0", "method": "notifications/initialized"},
                {"jsonrpc": "2.0", "id": 2, "method": "tools/call",
                 "params": {
                     "name": "hardware.capabilities", "arguments": {},
                 }},
            ])
            missing_pointer_by_id = {
                item["id"]: item for item in missing_pointer
            }
            missing_pointer_capabilities = json.loads(
                missing_pointer_by_id[2]["result"]["content"][0]["text"]
            )
            self.assertTrue(missing_pointer_capabilities["keyboard"])
            self.assertFalse(missing_pointer_capabilities["pointer"])
        finally:
            self.pointer.touch()

        self.assertNotIn("isError", by_id[4]["result"])
        self.assertNotIn("isError", by_id[5]["result"])
        self.assertTrue(by_id[6]["result"]["isError"])
        self.assertTrue(by_id[7]["result"]["isError"])
        image = by_id[8]["result"]["content"][0]
        self.assertEqual(image["mimeType"], "image/jpeg")
        self.assertEqual(base64.b64decode(image["data"]), JPEG)
        self.assertEqual(by_id[10]["error"]["code"], -32601)
        self.assertTrue(by_id[11]["result"]["isError"])
        self.assertEqual(
            self.hid_log.read_text(encoding="utf-8").splitlines(),
            ["key 4 2", "mouse -5 7 1 -1", "key 0 0", "mouse 0 0 0 0"],
        )

        malformed = run([self.mcpd], input_data=b"{bad json\n").stdout
        parse_error = json.loads(malformed)
        self.assertIsNone(parse_error["id"])
        self.assertEqual(parse_error["error"]["code"], -32700)
        trailing = run(
            [self.mcpd],
            input_data=b'{"jsonrpc":"2.0","id":1,"method":"initialize",'
                       b'"params":{}} trailing\n',
        ).stdout
        self.assertEqual(json.loads(trailing)["error"]["code"], -32700)
        oversized = run(
            [self.mcpd],
            input_data=b"{" + b"x" * (1024 * 1024) + b"}\n",
        ).stdout
        self.assertIn(b"Request exceeds 1 MiB", oversized)
        before_initialize = self.mcp([
            {"jsonrpc": "2.0", "id": "early", "method": "tools/list"},
        ])[0]
        self.assertEqual(before_initialize["error"]["code"], -32002)

        self.hid_log.write_text("", encoding="utf-8")
        self.agent_lock.write_text(str(os.getpid()) + "\n", encoding="ascii")
        owned = self.mcp([
            {"jsonrpc": "2.0", "id": 1, "method": "initialize",
             "params": {"protocolVersion": "2025-11-25"}},
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            {"jsonrpc": "2.0", "id": 2, "method": "tools/call",
             "params": {"name": "computer.key",
                        "arguments": {"keycode": 4}}},
            {"jsonrpc": "2.0", "id": 3, "method": "tools/call",
             "params": {"name": "safety.stop", "arguments": {}}},
        ])
        owned_by_id = {item["id"]: item for item in owned}
        self.assertTrue(owned_by_id[2]["result"]["isError"])
        self.assertNotIn("isError", owned_by_id[3]["result"])
        self.assertTrue(self.stop_file.exists())
        self.assertEqual(
            self.hid_log.read_text(encoding="utf-8").splitlines(),
            ["key 0 0", "mouse 0 0 0 0"],
        )

    def test_user_mcp_client_local_workflow(self):
        result = run([
            "python3", MCP_CLIENT,
            "--transport", "local", "--program", self.mcpd, "tools",
        ])
        self.assertIn(b"computer.capture", result.stdout)
        self.assertIn(b"safety.stop", result.stdout)

        result = run([
            "python3", MCP_CLIENT,
            "--transport", "local", "--program", self.mcpd,
            "key", "4", "--modifier", "2",
        ])
        self.assertEqual(result.stdout.strip(), b"OK")

        output = self.runtime / "client-capture.jpg"
        with CaptureServer(self.capture_socket, self.screen):
            run([
                "python3", MCP_CLIENT,
                "--transport", "local", "--program", self.mcpd,
                "capture", "-o", output,
            ])
        self.assertEqual(output.read_bytes(), JPEG)

    def run_agent(
        self, responses, *, max_steps=3, use_base_url=False,
        task="open terminal",
    ):
        key = "sk-host-testing-123"
        self.key_file.write_text(key + "\n", encoding="utf-8")
        self.key_file.chmod(0o600)
        with ModelServer(self.tls_cert, self.tls_key, responses, key) as server:
            endpoint = server.endpoint
            if use_base_url:
                endpoint = endpoint[:-len("/chat/completions")]
            self.config_file.write_text(
                "ENDPOINT={}\nMODEL=test-vision\n".format(endpoint),
                encoding="utf-8",
            )
            self.config_file.chmod(0o600)
            env = os.environ.copy()
            env["CURL_CA_BUNDLE"] = str(self.tls_cert)
            result = run(
                [self.agentd, "run", task, str(max_steps)],
                env=env,
                check=False,
            )
        return result, server.requests, server.paths

    def test_agent_https_visual_loop_and_release(self):
        result, requests, paths = self.run_agent([
            "```json\n{\"action\":\"key\",\"keycode\":4,\"modifier\":2}\n```",
            "{\"action\":\"mouse\",\"dx\":5,\"dy\":-3,\"buttons\":1,\"wheel\":0}",
            "{\"action\":\"done\",\"reason\":\"visible\"}",
        ])
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertFalse(self.agent_lock.exists())
        self.assertIn(b'"type":"done"', result.stdout)
        self.assertIn(b'"message":"visible"', result.stdout)
        self.assertEqual(len(requests), 3)
        for _headers, body in requests:
            self.assertEqual(body["model"], "test-vision")
            image_url = body["messages"][1]["content"][1]["image_url"]["url"]
            self.assertTrue(image_url.startswith("data:image/jpeg;base64,"))
        self.assertEqual(paths, ["/v1/chat/completions"] * 3)
        self.assertEqual(
            self.hid_log.read_text(encoding="utf-8").splitlines(),
            [
                "mouse 5 -3 1 0",
                "mouse 0 0 0 0",
            ],
        )
        self.assertEqual(
            self.keyboard.read_bytes(),
            b"\x00" * 8 + b"\x02\x00\x04" + b"\x00" * 5 + b"\x00" * 8,
        )

    def test_agent_rejects_out_of_range_and_trailing_model_output(self):
        result, _, _ = self.run_agent([
            "{\"action\":\"key\",\"keycode\":102,\"modifier\":0}",
        ], max_steps=1)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"Rejected an invalid keyboard action", result.stdout)
        self.hid_log.write_text("", encoding="utf-8")
        result, _, _ = self.run_agent([
            "{\"action\":\"key\",\"keycode\":4,\"modifier\":\"2\",\"extra\":1}",
        ], max_steps=1)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"Rejected an invalid keyboard action", result.stdout)
        self.hid_log.write_text("", encoding="utf-8")
        result, _, _ = self.run_agent([
            "{\"action\":\"done\",\"reason\":\"x\"} trailing prose",
            "{\"action\":\"done\",\"reason\":\"x\"} trailing prose",
        ], max_steps=1)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"The model returned an invalid action", result.stdout)

    def test_agent_retries_one_malformed_model_response(self):
        result, requests, _ = self.run_agent([
            "I should click the icon.",
            "{\"action\":\"done\",\"reason\":\"visible after retry\"}",
        ], max_steps=1)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 2)
        self.assertIn(b'"type":"retry"', result.stdout)
        retry_prompt = requests[1][1]["messages"][1]["content"][0]["text"]
        self.assertIn("previous response was not one valid action", retry_prompt)
        system_prompt = requests[0][1]["messages"][0]["content"]
        self.assertIn("relative USB pointer", system_prompt)
        self.assertIn("action history", system_prompt)
        self.assertIn("netease_search", system_prompt)
        self.assertIn("same HID action more than twice", system_prompt)
        self.assertIn("large Play button is not proof", system_prompt)
        self.assertIn("bottom player visibly shows", system_prompt)
        self.assertIn("tiny track-name OCR as uncertain", system_prompt)

    def test_agent_blocks_third_identical_action(self):
        repeated = (
            '{"observation":"same frame","reason":"try tab",'
            '"action":"hotkey","keys":"TAB"}'
        )
        result, requests, _ = self.run_agent([
            repeated,
            repeated,
            repeated,
            '{"observation":"recovered","action":"done","reason":"visible"}',
        ], max_steps=4)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 4)
        self.assertIn(
            b"Blocked a third consecutive identical action",
            result.stdout,
        )
        self.assertIn(
            b"Rejected repeated action: choose a different recovery",
            requests[3][1]["messages"][1]["content"][0]["text"].encode(),
        )

    def test_agent_launches_browser_once_then_requires_window_recovery(self):
        result, requests, _ = self.run_agent([
            (
                '{"observation":"Windows desktop; Edge is not visible",'
                '"reason":"launch browser once","action":"launch_browser"}'
            ),
            (
                '{"observation":"Windows desktop; browser absent",'
                '"reason":"inspect another window","action":"window_cycle"}'
            ),
            (
                '{"observation":"VMware window; Edge browser is absent",'
                '"reason":"cycle again","action":"window_cycle"}'
            ),
            (
                '{"observation":"VMware window with an Edge browser icon on '
                'the taskbar","reason":"probe cursor","action":"cursor_probe"}'
            ),
            (
                '{"observation":"Windows desktop; browser absent",'
                '"reason":"probe cursor again","action":"cursor_probe"}'
            ),
            (
                '{"observation":"Microsoft Edge browser window is visible",'
                '"action":"done","reason":"browser visible on HDMI"}'
            ),
        ], max_steps=6, task="open the Edge browser")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 6)
        self.assertEqual(
            result.stdout.count(b"Prepared one browser launch through Windows Search"),
            1,
        )
        self.assertIn(
            b"Replaced a repeated window cycle with the bounded move phase",
            result.stdout,
        )
        self.assertIn(
            b"Automatically moved the cycled window right",
            result.stdout,
        )
        self.assertEqual(
            result.stdout.count(b"Completed bounded cursor visibility sweep"),
            1,
        )
        self.assertIn(b"Released VMware input capture", result.stdout)
        self.assertIn(b"Cycled one window for HDMI inspection", result.stdout)

    def test_agent_chinese_browser_task_ignores_taskbar_icon(self):
        result, requests, _ = self.run_agent([
            (
                '{"observation":"当前显示VMware窗口，任务栏有Edge浏览器图标，'
                '没有浏览器窗口","reason":"启动浏览器",'
                '"action":"launch_browser"}'
            ),
            (
                '{"observation":"Microsoft Edge浏览器窗口显示哔哩哔哩'
                '搜索页面和韦东山视频结果","action":"done",'
                '"reason":"目标页面可见"}'
            ),
        ], max_steps=2, task="打开浏览器，在哔哩哔哩查找韦东山视频")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 2)
        self.assertEqual(
            result.stdout.count(b"Prepared one browser launch through Windows Search"),
            1,
        )

    def test_agent_uses_visibly_verified_taskbar_browser_fallback(self):
        result, requests, _ = self.run_agent([
            (
                '{"observation":"Windows desktop; browser window absent",'
                '"reason":"launch once","action":"launch_browser"}'
            ),
            (
                '{"observation":"VMware window; taskbar shows Edge browser '
                'icon; cursor hidden","reason":"expose cursor",'
                '"action":"cursor_probe"}'
            ),
            (
                '{"observation":"taskbar shows Edge browser icon; cursor '
                'visible near the left side of the Edge icon",'
                '"reason":"move toward icon",'
                '"action":"cursor_move","direction":"right","amount":8}'
            ),
            (
                '{"observation":"taskbar shows Edge browser icon; cursor is '
                'over the Edge icon","reason":"open verified browser icon",'
                '"action":"cursor_click","button":1}'
            ),
            (
                '{"observation":"Microsoft Edge browser window shows '
                'Bilibili homepage","action":"done",'
                '"reason":"Bilibili page is visible"}'
            ),
        ], max_steps=5, task="open Bilibili in the browser")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 5)
        self.assertNotIn(
            b"Replaced an unsafe browser action",
            result.stdout,
        )
        self.assertIn(
            b"Clicked only after visible cursor-target verification",
            result.stdout,
        )

    def test_agent_releases_visible_vmware_before_browser_launch(self):
        result, requests, _ = self.run_agent([
            (
                '{"observation":"VMware Workstation window is active; '
                'taskbar shows Edge browser icon; browser window absent",'
                '"reason":"launch Windows browser",'
                '"action":"launch_browser"}'
            ),
            (
                '{"observation":"Microsoft Edge browser window shows '
                'Bilibili homepage","action":"done",'
                '"reason":"Bilibili page is visible"}'
            ),
        ], max_steps=2, task="open Bilibili in the browser")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 2)
        self.assertIn(
            b"Released visible VMware input capture with Ctrl+Alt",
            result.stdout,
        )
        self.assertIn(
            b"Prepared one browser launch through Windows Search",
            result.stdout,
        )

    def test_agent_submits_only_visibly_verified_edge_search_result(self):
        result, requests, _ = self.run_agent([
            (
                '{"observation":"Windows desktop; browser absent",'
                '"reason":"prepare browser launch","action":"launch_browser"}'
            ),
            (
                '{"observation":"Windows desktop; Microsoft Edge is not '
                'visible in a Search panel","reason":"try submit",'
                '"action":"browser_launch_submit"}'
            ),
            (
                '{"observation":"Windows Search panel visibly shows '
                'Microsoft Edge as the Best match app result",'
                '"reason":"submit verified result",'
                '"action":"browser_launch_submit"}'
            ),
            (
                '{"observation":"Microsoft Edge browser window shows '
                'Bilibili homepage","action":"done",'
                '"reason":"Bilibili page is visible"}'
            ),
        ], max_steps=4, task="open Bilibili in the browser")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 4)
        self.assertIn(b"Blocked browser launch submission", result.stdout)
        self.assertIn(
            b"Submitted the visibly verified Edge Windows Search result",
            result.stdout,
        )

    def test_agent_visibly_selects_edge_before_taskbar_activation(self):
        result, requests, _ = self.run_agent([
            (
                '{"observation":"Windows desktop; Edge icon is visible on '
                'the taskbar; browser window absent","reason":"focus taskbar",'
                '"action":"taskbar_cycle"}'
            ),
            (
                '{"observation":"Microsoft Edge taskbar icon is highlighted '
                'and focused","reason":"activate selected browser",'
                '"action":"taskbar_activate"}'
            ),
            (
                '{"observation":"Microsoft Edge browser window shows '
                'Bilibili homepage","action":"done",'
                '"reason":"Bilibili page is visible"}'
            ),
        ], max_steps=3, task="open Bilibili in the browser")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 3)
        self.assertIn(
            b"Focused the next bounded Windows taskbar item",
            result.stdout,
        )
        self.assertIn(
            b"Activated only the visibly selected Edge taskbar icon",
            result.stdout,
        )

    def test_agent_requires_visible_edge_icon_for_managed_shortcut(self):
        result, requests, _ = self.run_agent([
            (
                '{"observation":"Windows taskbar is visible; browser absent",'
                '"reason":"try app shortcut",'
                '"action":"edge_taskbar_shortcut"}'
            ),
            (
                '{"observation":"Windows taskbar visibly shows the Microsoft '
                'Edge icon","reason":"launch managed app slot",'
                '"action":"edge_taskbar_shortcut"}'
            ),
            (
                '{"observation":"Microsoft Edge browser window shows '
                'Bilibili homepage","action":"done",'
                '"reason":"Bilibili page is visible"}'
            ),
        ], max_steps=3, task="open Bilibili in the browser")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 3)
        self.assertIn(b"Blocked Edge taskbar shortcut", result.stdout)
        self.assertIn(
            b"Focused the managed Edge Win+2 slot only after visibly",
            result.stdout,
        )

    def test_agent_prioritizes_visible_managed_edge_over_generic_recovery(self):
        result, requests, _ = self.run_agent([
            (
                '{"observation":"Windows Settings is open; browser absent",'
                '"reason":"prepare browser","action":"launch_browser"}'
            ),
            (
                '{"observation":"Windows Settings is open; the Edge icon is '
                'visible on the taskbar; cursor hidden",'
                '"reason":"expose cursor","action":"cursor_probe"}'
            ),
            (
                '{"observation":"Microsoft Edge browser window shows '
                'Bilibili homepage","action":"done",'
                '"reason":"Bilibili page is visible"}'
            ),
        ], max_steps=3, task="open Bilibili in the browser")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 3)
        self.assertIn(
            b"Replaced generic browser recovery with the visibly anchored",
            result.stdout,
        )
        self.assertIn(
            b"Focused the managed Edge Win+2 slot after the current frame",
            result.stdout,
        )
        self.assertNotIn(
            b"Completed bounded cursor visibility sweep",
            result.stdout,
        )

    def test_agent_moves_just_focused_managed_edge_into_hdmi(self):
        result, requests, _ = self.run_agent([
            (
                '{"observation":"Windows Settings is open; browser absent",'
                '"reason":"prepare browser","action":"launch_browser"}'
            ),
            (
                '{"observation":"Windows Settings is open; the Edge icon is '
                'visible on the taskbar; cursor hidden",'
                '"reason":"expose cursor","action":"cursor_probe"}'
            ),
            (
                '{"observation":"Windows Settings remains visible; browser '
                'absent","reason":"inspect another window",'
                '"action":"window_cycle"}'
            ),
            (
                '{"observation":"Microsoft Edge browser window shows '
                'Bilibili homepage","action":"done",'
                '"reason":"Bilibili page is visible"}'
            ),
        ], max_steps=4, task="open Bilibili in the browser")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 4)
        self.assertIn(
            b"moving the just-focused managed Edge window right",
            result.stdout,
        )
        self.assertIn(
            b"Moved the managed Edge window right for fresh-frame",
            result.stdout,
        )

    def test_agent_prepares_and_visibly_verifies_browser_url(self):
        result, requests, _ = self.run_agent([
            (
                '{"observation":"Microsoft Edge address bar shows a Chinese IME '
                'candidate list and garbled text","reason":"enter URL",'
                '"action":"browser_url_prepare",'
                '"url":"https://www.bilibili.com"}'
            ),
            (
                '{"observation":"Microsoft Edge address bar shows Chinese IME '
                'candidates","reason":"switch input mode",'
                '"action":"normalize_ime"}'
            ),
            (
                '{"observation":"Microsoft Edge browser window and address bar '
                'are visible with English input","reason":"prepare URL",'
                '"action":"browser_url_prepare",'
                '"url":"https://www.bilibili.com"}'
            ),
            (
                '{"observation":"Microsoft Edge address bar exactly shows '
                'https://www.bilibili.com","reason":"submit verified URL",'
                '"action":"browser_url_submit"}'
            ),
            (
                '{"observation":"Bilibili homepage page is visible",'
                '"action":"done","reason":"target site visible"}'
            ),
        ], max_steps=5, task="open Bilibili in the browser")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 5)
        self.assertIn(b"Blocked URL typing", result.stdout)
        self.assertIn(b"Toggled the visible browser input mode", result.stdout)
        self.assertIn(
            b"Prepared a browser URL without submitting",
            result.stdout,
        )
        self.assertIn(
            b"Closed suggestions and submitted the address after visible URL verification",
            result.stdout,
        )

    def test_agent_visually_steers_relative_cursor_before_click(self):
        result, requests, _ = self.run_agent([
            (
                '{"observation":"Bilibili search results page; cursor hidden",'
                '"reason":"expose cursor","action":"cursor_probe"}'
            ),
            (
                '{"observation":"Bilibili search results page; visible cursor '
                'is left of the target video","reason":"move locally",'
                '"action":"cursor_move","direction":"right","amount":12}'
            ),
            (
                '{"observation":"Bilibili search results page; cursor is over '
                'the target video thumbnail","reason":"open verified result",'
                '"action":"cursor_click","button":1}'
            ),
            (
                '{"observation":"Bilibili video page and target title are visible",'
                '"action":"done","reason":"target video opened"}'
            ),
        ], max_steps=4, task="open a Bilibili search result in the browser")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 4)
        reports = self.hid_log.read_text(encoding="utf-8").splitlines()
        self.assertIn("mouse 12 0 0 0", reports)
        self.assertIn("mouse 0 0 1 0", reports)
        self.assertIn(
            b"Clicked only after visible cursor-target verification",
            result.stdout,
        )

    def test_agent_uses_verified_direct_bilibili_search_for_weidongshan(self):
        result, requests, _ = self.run_agent([
            (
                '{"observation":"Bilibili homepage page is visible with a '
                'search box; cursor hidden","reason":"expose cursor",'
                '"action":"cursor_probe"}'
            ),
            (
                '{"observation":"Edge address bar shows the verified '
                'Bilibili search URL for weidongshan",'
                '"reason":"submit verified search URL",'
                '"action":"browser_url_submit"}'
            ),
            (
                '{"observation":"Bilibili search results page visibly shows '
                'Wei Dongshan videos","action":"done",'
                '"reason":"target search results are visible"}'
            ),
        ], max_steps=3, task="find weidongshan videos on Bilibili in browser")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 3)
        self.assertIn(
            b"Replaced unreliable homepage pointer search",
            result.stdout,
        )
        self.assertIn(
            b"Prepared the Wei Dongshan Bilibili search URL without submitting",
            result.stdout,
        )

    def test_agent_allows_four_bounded_hidden_cursor_probes(self):
        probe = (
            '{"observation":"cursor hidden on the current display",'
            '"reason":"bounded visibility search","action":"cursor_probe"}'
        )
        result, requests, _ = self.run_agent([
            probe,
            probe,
            probe,
            probe,
            '{"observation":"cursor search exhausted",'
            '"action":"done","reason":"bounded search finished"}',
        ], max_steps=5, task="locate the pointer without clicking")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 5)
        self.assertEqual(
            result.stdout.count(b"Completed bounded cursor visibility sweep"),
            4,
        )
        self.assertNotIn(
            b"Blocked a third consecutive identical action",
            result.stdout,
        )

    def test_agent_rejects_bilibili_completion_without_visible_playback(self):
        result, requests, _ = self.run_agent([
            (
                '{"observation":"Bilibili search results page shows a '
                '\u97e6\u4e1c\u5c71 video but it is not playing",'
                '"action":"done","reason":"result found"}'
            ),
            (
                '{"observation":"Bilibili video player is playing '
                '\u97e6\u4e1c\u5c71; speaker icon is visible and progress advanced",'
                '"action":"done","reason":"playback visibly verified"}'
            ),
        ], max_steps=2, task="\u6253\u5f00 bilibili \u64ad\u653e\u97e6\u4e1c\u5c71\u89c6\u9891")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 2)
        self.assertIn(
            b"Blocked completion because playback is not visibly verified",
            result.stdout,
        )

    def test_agent_runs_atomic_netease_search(self):
        result, requests, _ = self.run_agent([
            (
                '{"observation":"NetEase window is visible","reason":"search artist",'
                '"action":"netease_search","text":"xuliang"}'
            ),
            (
                '{"observation":"NetEase bottom player is playing xuliang with '
                'pause bars","action":"done","reason":"playback visible"}'
            ),
        ], max_steps=2, task="open NetEase and play xuliang music")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 2)
        self.assertIn(
            b"Searched the NetEase window already visible in the current HDMI "
            b"frame with the validated ASCII query",
            result.stdout,
        )

    def test_agent_gates_netease_actions_on_current_hdmi_visibility(self):
        result, requests, _ = self.run_agent([
            (
                '{"observation":"Windows desktop; NetEase is not visible",'
                '"reason":"try search","action":"netease_search",'
                '"text":"xuliang"}'
            ),
            (
                '{"observation":"桌面，未看到网易云窗口",'
                '"reason":"focus taskbar","action":"hotkey","keys":"WIN+T"}'
            ),
            (
                '{"observation":"设备管理器窗口可见，未看到网易云窗口",'
                '"reason":"close unrelated window","action":"hotkey",'
                '"keys":"ALT+F4"}'
            ),
            (
                '{"observation":"桌面，未看到网易云窗口",'
                '"reason":"move hidden app","action":"move_active_window",'
                '"direction":"right"}'
            ),
            (
                '{"observation":"桌面，未看到网易云窗口",'
                '"reason":"inspect another window","action":"window_cycle"}'
            ),
            (
                '{"observation":"桌面，未看到网易云窗口",'
                '"reason":"bring cycled window into frame",'
                '"action":"move_active_window","direction":"right"}'
            ),
            (
                '{"observation":"网易云音乐窗口已显示搜索界面",'
                '"reason":"search artist","action":"netease_search",'
                '"text":"xuliang"}'
            ),
            (
                '{"observation":"网易云窗口底部播放器正在播放徐良，显示暂停按钮",'
                '"action":"done","reason":"播放状态在当前HDMI帧可见"}'
            ),
        ], max_steps=8, task="打开网易云播放徐良的音乐")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 8)
        self.assertIn(
            b"Blocked NetEase search because the current observation does not "
            b"prove its window is visible on HDMI",
            result.stdout,
        )
        self.assertIn(b"Blocked blind NetEase hotkey", result.stdout)
        self.assertIn(b"Rejected window move: cycle a window first", result.stdout)
        self.assertIn(b"Cycled one window for HDMI inspection", result.stdout)
        self.assertIn(b"Moved the cycled active window right", result.stdout)

    def test_agent_rejects_netease_completion_without_visible_playback(self):
        result, requests, _ = self.run_agent([
            (
                '{"observation":"网易云搜索结果窗口可见但尚未播放",'
                '"action":"done","reason":"search results visible"}'
            ),
            (
                '{"observation":"NetEase bottom player is visible with a play button",'
                '"action":"done","reason":"player visible"}'
            ),
            (
                '{"observation":"网易云窗口底部播放器正在播放，显示暂停按钮",'
                '"action":"done","reason":"playback visible"}'
            ),
        ], max_steps=3, task="打开网易云播放音乐")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 3)
        self.assertEqual(
            result.stdout.count(
                b"Blocked completion because playback is not visibly verified"
            ),
            2,
        )

    def test_agent_limits_netease_searches_across_other_actions(self):
        search = (
            '{"observation":"NetEase is visible","reason":"search artist",'
            '"action":"netease_search","text":"xuliang"}'
        )
        result, requests, _ = self.run_agent([
            search,
            '{"observation":"loading","reason":"wait","action":"wait"}',
            search,
            '{"observation":"unchanged","reason":"select","action":"hotkey",'
            '"keys":"CTRL+A"}',
            search,
            '{"observation":"recovered","action":"done","reason":"visible"}',
        ], max_steps=6)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 6)
        self.assertIn(
            b"Blocked more than two NetEase searches in one task",
            result.stdout,
        )
        self.assertIn(
            b"Rejected NetEase search: task limit of two reached",
            requests[5][1]["messages"][1]["content"][0]["text"].encode(),
        )

    def test_agent_rejects_absolute_click_then_uses_keyboard_actions(self):
        result, requests, _ = self.run_agent([
            "{\"action\":\"click\",\"x\":480,\"y\":270}",
            "{\"action\":\"type\",\"text\":\"A1\"}",
            "{\"action\":\"hotkey\",\"keys\":\"CTRL+L\"}",
            "{\"action\":\"scroll\",\"wheel\":-3}",
            "{\"action\":\"done\",\"reason\":\"visible\"}",
        ], max_steps=5)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(requests), 5)
        second_prompt = requests[1][1]["messages"][1]["content"][0]["text"]
        self.assertIn(
            "Step 1 executed: Rejected click: use keyboard navigation",
            second_prompt,
        )
        self.assertEqual(
            self.hid_log.read_text(encoding="utf-8").splitlines(),
            [
                "mouse 0 0 0 -3",
                "mouse 0 0 0 0",
            ],
        )
        self.assertIn(b"Typed validated ASCII text", result.stdout)
        self.assertIn(b"Named keyboard shortcut sent", result.stdout)
        self.assertEqual(
            self.keyboard.read_bytes(),
            b"\x00" * 8 + b"\x01\x00\x0f" + b"\x00" * 5 + b"\x00" * 8,
        )

    def test_agent_converts_netease_unicode_to_pinyin(self):
        result, _requests, _ = self.run_agent([
            '{"action":"type","text":"网易云音乐"}',
            '{"action":"done","reason":"typed"}',
        ], max_steps=2)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertIn(b"Typed validated pinyin alias", result.stdout)
        self.assertEqual(
            self.hid_log.read_text(encoding="utf-8").splitlines(),
            [
                "mouse 0 0 0 0",
            ],
        )
        self.assertEqual(
            self.keyboard.read_bytes(),
            b"\x00" * 8 + b"\x00\x00\x06" + b"\x00" * 5 + b"\x00" * 8,
        )

    def test_agent_accepts_openai_base_url(self):
        result, _requests, paths = self.run_agent([
            "{\"action\":\"done\",\"reason\":\"base URL works\"}",
        ], max_steps=1, use_base_url=True)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(paths, ["/v1/chat/completions"])

    def test_agent_rejects_insecure_secret_permissions(self):
        self.key_file.write_text("sk-insecure-testing\n", encoding="utf-8")
        self.key_file.chmod(0o644)
        self.config_file.write_text(
            "ENDPOINT=https://localhost/v1\nMODEL=test\n", encoding="utf-8"
        )
        self.config_file.chmod(0o600)
        result = run(
            [self.agentd, "run", "do nothing", "1"],
            check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"configure key", result.stderr)

    def make_app(self, destination, *, version="1.0.0", permissions=None):
        shutil.copytree(EXAMPLE, destination)
        manifest_path = destination / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["version"] = version
        if permissions is not None:
            manifest["permissions"] = permissions
        manifest_path.write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        return destination

    def build_app(self, source, output, key=None, check=True):
        return run([
            "python3", AITAPP, "build", source,
            "--key", key or self.signing_key, "-o", output,
        ], check=check)

    def test_trusted_app_build_verify_and_policy_rejection(self):
        root = self.workspace / "app-policy"
        shutil.rmtree(root, ignore_errors=True)
        app = self.make_app(root)
        package = self.runtime / "hello-policy.aitapp"
        self.build_app(app, package)
        with tarfile.open(package, "r:gz") as archive:
            for member in archive.getmembers():
                self.assertEqual((member.uid, member.gid), (0, 0))
                self.assertEqual((member.uname, member.gname), ("root", "root"))
        result = run([
            "python3", AITAPP, "verify", package,
            "--public-key", self.public_key,
        ])
        self.assertIn(b"OK", result.stdout)
        requests = []
        package_bytes = package.read_bytes()

        class UploadHandler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *_args):
                pass

            def do_GET(self):
                requests.append((self.command, self.path, b"", self.headers))
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({
                    "isSetup": True,
                    "passwordRequired": True,
                }).encode("utf-8"))

            def do_POST(self):
                length = int(self.headers.get("Content-Length", "0"))
                body = self.rfile.read(length)
                requests.append((self.command, self.path, body, self.headers))
                if self.path == "/auth/login-local":
                    if json.loads(body) != {"password": "test-device-password"}:
                        self.send_error(403)
                        return
                    self.send_response(200)
                    self.send_header("Set-Cookie", "authToken=test-token; Path=/")
                    self.send_header("Content-Type", "application/json")
                    self.end_headers()
                    self.wfile.write(b'{"message":"Login successful"}')
                    return
                if self.headers.get("Cookie") != "authToken=test-token":
                    self.send_error(401)
                    return
                self.send_response(201)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(
                    b'{"message":"Application installed","app":'
                    b'{"id":"com.example.hello","version":"1.0.0"}}'
                )

        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), UploadHandler)
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()
        try:
            environment = os.environ.copy()
            environment["AITVBOX_PASSWORD"] = "test-device-password"
            result = run([
                "python3", AITAPP, "upload", package,
                "--public-key", self.public_key,
                "--url", "http://127.0.0.1:{}".format(server.server_port),
                "--replace",
            ], env=environment)
        finally:
            server.shutdown()
            server.server_close()
            server_thread.join(timeout=5)
        self.assertIn(b"OK com.example.hello 1.0.0", result.stdout)
        self.assertEqual(
            [(method, path) for method, path, _body, _headers in requests],
            [
                ("GET", "/device/status"),
                ("POST", "/auth/login-local"),
                ("POST", "/api/apps/install?mode=replace"),
            ],
        )
        upload_body = requests[2][2]
        self.assertIn(package.name.encode("utf-8"), upload_body)
        self.assertIn(package_bytes, upload_body)

        manifest_path = app / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["permissions"] = []
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        result = self.build_app(app, package, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"requires manifest permission", result.stderr)

        manifest["permissions"] = ["hardware.gpio.read"]
        manifest["ui"]["presentation"] = "fullscreen"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        result = self.build_app(app, package, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"only embedded presentation", result.stderr)

        manifest["ui"]["presentation"] = "embedded"
        manifest["permissions"] = ["input.keyboard"]
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        ui_path = app / "ui/main.json"
        ui = json.loads(ui_path.read_text(encoding="utf-8"))
        ui["layout"]["children"][1]["command"] = "input.keyboard"
        ui_path.write_text(json.dumps(ui), encoding="utf-8")
        result = self.build_app(app, package, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"only capture.snapshot", result.stderr)

        ui["layout"]["children"][1]["command"] = "capture.snapshot"
        ui_path.write_text(json.dumps(ui), encoding="utf-8")
        manifest["permissions"] = ["capture.snapshot"]
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        script = app / "payload.sh"
        script.write_text("#!/bin/sh\n", encoding="utf-8")
        script.chmod(0o755)
        result = self.build_app(app, package, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"executables and symlinks", result.stderr)

    def test_user_app_init_build_verify_workflow(self):
        app = self.workspace / "initialized-app"
        shutil.rmtree(app, ignore_errors=True)
        run([
            "python3", AITAPP, "init", app,
            "--id", "com.example.status",
            "--name", "Status Panel",
            "--publisher", "example-dev",
        ])
        manifest = json.loads((app / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["id"], "com.example.status")
        self.assertEqual(manifest["permissions"], [])
        package = self.runtime / "initialized.aitapp"
        self.build_app(app, package)
        result = run([
            "python3", AITAPP, "verify", package,
            "--public-key", self.public_key,
        ])
        self.assertIn(b"OK", result.stdout)

    def test_native_app_compile_build_and_atomic_install(self):
        if not TINA_SDK.is_dir():
            self.skipTest("A133 Tina SDK is unavailable")
        source = self.workspace / "native-package"
        shutil.rmtree(source, ignore_errors=True)
        shutil.copytree(
            NATIVE_EXAMPLE, source,
            ignore=shutil.ignore_patterns("bin"),
        )
        result = run([
            "python3", AITAPP, "compile", source, "--sdk", TINA_SDK,
        ])
        runtime = source / "bin/app"
        self.assertEqual(result.stdout.strip(), str(runtime).encode())
        self.assertEqual(runtime.read_bytes()[:4], b"\x7fELF")
        self.assertEqual(runtime.read_bytes()[18:20], b"\xb7\x00")

        package = self.runtime / "native-package.aitapp"
        self.build_app(source, package)
        result = run([
            "python3", AITAPP, "verify", package,
            "--public-key", self.public_key,
        ])
        self.assertIn(b"OK", result.stdout)

        app_root = self.runtime / "installed-native"
        trust_root = self.runtime / "trusted-native"
        shutil.rmtree(app_root, ignore_errors=True)
        shutil.rmtree(trust_root, ignore_errors=True)
        trust_root.mkdir()
        shutil.copy2(self.public_key, trust_root / "developer.pem")
        env = self.appctl_env(app_root, trust_root)
        result = run([APPCTL, "install", package], env=env)
        self.assertIn(b"OK com.100ask.native-status 1.0.0", result.stdout)
        installed = app_root / "com.100ask.native-status"
        self.assertEqual(
            (installed / "bin/app").stat().st_mode & 0o777, 0o555
        )
        self.assertEqual(
            (installed / "manifest.json").stat().st_mode & 0o777, 0o444
        )

        extra = source / "ui/hidden.json"
        extra.write_bytes(b"\x7fELF" + b"\x00" * 64)
        result = self.build_app(source, package, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"extra ELF payloads are prohibited", result.stderr)

    def appctl_env(self, app_root, trust_root):
        env = os.environ.copy()
        env["PATH"] = str(self.bin_dir) + os.pathsep + env["PATH"]
        env["AITVBOX_APP_ROOT"] = str(app_root)
        env["AITVBOX_TRUST_ROOT"] = str(trust_root)
        env["AITVBOX_APP_POLICY"] = str(self.app_policy)
        return env

    def test_device_install_update_preserves_data_and_rejects_tamper(self):
        source = self.workspace / "app-install"
        shutil.rmtree(source, ignore_errors=True)
        self.make_app(source)
        package = self.runtime / "hello-install.aitapp"
        self.build_app(source, package)
        app_root = self.runtime / "installed"
        trust_root = self.runtime / "trusted"
        shutil.rmtree(app_root, ignore_errors=True)
        shutil.rmtree(trust_root, ignore_errors=True)
        app_root.mkdir()
        abandoned = app_root / ".install.abandoned"
        abandoned.mkdir()
        (abandoned / "partial").write_text("partial\n", encoding="ascii")
        stale_lock = app_root / ".appctl.lock"
        stale_lock.mkdir()
        trust_root.mkdir()
        shutil.copy2(self.public_key, trust_root / "developer.pem")
        env = self.appctl_env(app_root, trust_root)

        result = run([APPCTL, "list"], env=env, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"operation is initializing", result.stderr)
        self.assertTrue(stale_lock.exists())
        os.utime(stale_lock, (1, 1))
        run([APPCTL, "list"], env=env)
        self.assertFalse(stale_lock.exists())
        self.assertFalse(abandoned.exists())

        stale_lock.mkdir()
        (stale_lock / "pid").write_text("999999\n", encoding="ascii")
        result = run([APPCTL, "install", package], env=env)
        self.assertEqual(result.stdout.strip(), b"OK com.100ask.hello 1.0.0")
        self.assertFalse(stale_lock.exists())
        data = app_root / "com.100ask.hello/data/state.json"
        data.parent.mkdir(exist_ok=True)
        data.write_text('{"count":1}\n', encoding="utf-8")

        manifest_path = source / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["version"] = "1.1.0"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        (source / "ui/main.json").write_text(
            (source / "ui/main.json").read_text(encoding="utf-8").replace(
                "Trusted declarative application", "Updated application"
            ),
            encoding="utf-8",
        )
        self.build_app(source, package)
        run([APPCTL, "install", package], env=env)
        self.assertEqual(data.read_text(encoding="utf-8"), '{"count":1}\n')
        self.assertFalse(any(path.name.startswith((".install.", ".old."))
                             for path in app_root.iterdir()))

        extracted = self.runtime / "tampered-app"
        shutil.rmtree(extracted, ignore_errors=True)
        extracted.mkdir()
        with tarfile.open(package, "r:gz") as archive:
            archive.extractall(extracted)
        (extracted / "extra.json").write_text("{}\n", encoding="utf-8")
        tampered = self.runtime / "tampered.aitapp"
        with tarfile.open(tampered, "w:gz") as archive:
            for path in sorted(extracted.iterdir()):
                archive.add(path, arcname=path.name)
        result = run([APPCTL, "install", tampered], env=env, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"checksum list does not match", result.stderr)

        untrusted = self.runtime / "untrusted.aitapp"
        self.build_app(source, untrusted, key=self.other_key)
        result = run([APPCTL, "install", untrusted], env=env, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"publisher key is not trusted", result.stderr)

        symlink_package = self.runtime / "symlink.aitapp"
        with tarfile.open(package, "r:gz") as source_archive, \
                tarfile.open(symlink_package, "w:gz") as output_archive:
            for member in source_archive.getmembers():
                stream = source_archive.extractfile(member) if member.isfile() else None
                output_archive.addfile(member, stream)
            link = tarfile.TarInfo("ui/link.json")
            link.type = tarfile.SYMTYPE
            link.linkname = "../../outside"
            output_archive.addfile(link)
        result = run([APPCTL, "install", symlink_package], env=env, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"unsafe archive", result.stderr)

        executable_package = self.runtime / "executable.aitapp"
        with tarfile.open(package, "r:gz") as source_archive, \
                tarfile.open(executable_package, "w:gz") as output_archive:
            for member in source_archive.getmembers():
                stream = source_archive.extractfile(member) if member.isfile() else None
                cloned = copy.copy(member)
                if cloned.name == "ui/main.json":
                    cloned.mode = 0o755
                output_archive.addfile(cloned, stream)
        result = run([APPCTL, "install", executable_package], env=env, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"executable or unsupported payload", result.stderr)

        run([APPCTL, "remove", "com.100ask.hello"], env=env)
        self.assertFalse((app_root / "com.100ask.hello").exists())

    def test_device_version_policy_recovery_and_publisher_ownership(self):
        source = self.workspace / "app-version"
        shutil.rmtree(source, ignore_errors=True)
        self.make_app(source, version="2.0.0")
        package = self.runtime / "hello-version.aitapp"
        self.build_app(source, package)
        app_root = self.runtime / "installed-version"
        trust_root = self.runtime / "trusted-version"
        shutil.rmtree(app_root, ignore_errors=True)
        shutil.rmtree(trust_root, ignore_errors=True)
        trust_root.mkdir()
        shutil.copy2(self.public_key, trust_root / "developer.pem")
        shutil.copy2(self.other_public_key, trust_root / "other.pem")
        env = self.appctl_env(app_root, trust_root)

        run([APPCTL, "install", package], env=env)
        result = run([APPCTL, "install", package], env=env, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"same-version reinstall requires --replace", result.stderr)
        run([APPCTL, "install", "--replace", package], env=env)

        manifest_path = source / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["version"] = "1.9.0"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        self.build_app(source, package)
        result = run([APPCTL, "install", package], env=env, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"version downgrade requires --allow-downgrade", result.stderr)
        run([APPCTL, "install", "--allow-downgrade", package], env=env)

        target = app_root / "com.100ask.hello"
        interrupted = app_root / ".old.com.100ask.hello.999"
        target.rename(interrupted)
        manifest["version"] = "2.1.0"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        self.build_app(source, package)
        run([APPCTL, "install", package], env=env)
        self.assertTrue(target.is_dir())
        self.assertFalse(interrupted.exists())

        manifest["version"] = "2.2.0"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        self.build_app(source, package, key=self.other_key)
        result = run([APPCTL, "install", package], env=env, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"belongs to a different publisher key", result.stderr)

    def test_device_native_policy_rejects_signed_invalid_ui(self):
        source = self.workspace / "app-native-policy"
        shutil.rmtree(source, ignore_errors=True)
        self.make_app(source)
        package = self.runtime / "native-policy-base.aitapp"
        self.build_app(source, package)
        extracted = self.runtime / "native-policy-extracted"
        shutil.rmtree(extracted, ignore_errors=True)
        extracted.mkdir()
        with tarfile.open(package, "r:gz") as archive:
            archive.extractall(extracted)
        ui_path = extracted / "ui/main.json"
        ui = json.loads(ui_path.read_text(encoding="utf-8"))
        ui["layout"]["children"][1]["arguments"] = {"ignored": True}
        ui_path.write_text(json.dumps(ui), encoding="utf-8")
        payload = sorted(
            path for path in extracted.rglob("*")
            if path.is_file() and path.name not in {"SIGNATURE", "SHA256SUMS"}
        )
        lines = [
            "{}  {}".format(
                hashlib.sha256(path.read_bytes()).hexdigest(),
                path.relative_to(extracted).as_posix(),
            )
            for path in payload
        ]
        (extracted / "SHA256SUMS").write_text(
            "\n".join(lines) + "\n", encoding="ascii"
        )
        run([
            "openssl", "dgst", "-sha256", "-sign", self.signing_key,
            "-out", extracted / "SIGNATURE", extracted / "SHA256SUMS",
        ])
        invalid = self.runtime / "native-policy-invalid.aitapp"
        with tarfile.open(invalid, "w:gz") as archive:
            for path in sorted(extracted.rglob("*")):
                if path.is_file():
                    archive.add(path, arcname=path.relative_to(extracted).as_posix())

        app_root = self.runtime / "installed-native-policy"
        trust_root = self.runtime / "trusted-native-policy"
        shutil.rmtree(app_root, ignore_errors=True)
        shutil.rmtree(trust_root, ignore_errors=True)
        trust_root.mkdir()
        shutil.copy2(self.public_key, trust_root / "developer.pem")
        env = self.appctl_env(app_root, trust_root)
        result = run([APPCTL, "install", invalid], env=env, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"violates device policy", result.stderr)

    def test_agentctl_secret_and_stop_flow(self):
        secrets = self.runtime / "agentctl-secrets"
        shutil.rmtree(secrets, ignore_errors=True)
        env = os.environ.copy()
        env.update({
            "AITVBOX_SECRET_DIR": str(secrets),
            "AITVBOX_STOP_FILE": str(self.stop_file),
            "AITVBOX_HIDCTL": str(self.mock_hid),
        })
        result = run(
            [AGENTCTL, "set-key-stdin"],
            env=env,
            input_data=b"bad-key\n",
            check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        run(
            [AGENTCTL, "set-key-stdin"],
            env=env,
            input_data=b"ark-agentctl-testing\n",
        )
        self.assertEqual((secrets / "model-api-key").stat().st_mode & 0o777, 0o600)
        result = run(
            [AGENTCTL, "configure", "http://localhost/v1", "model"],
            env=env,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        run(
            [AGENTCTL, "configure", "https://localhost/v1", "vision-model"],
            env=env,
        )
        status = run([AGENTCTL, "status"], env=env)
        self.assertEqual(status.stdout.strip(), b"key=configured config=configured")
        run([AGENTCTL, "stop"], env=env)
        self.assertTrue(self.stop_file.exists())
        self.assertEqual(
            self.hid_log.read_text(encoding="utf-8").splitlines(),
            ["key 0 0", "mouse 0 0 0 0"],
        )
        run([AGENTCTL, "clear"], env=env)
        self.assertFalse((secrets / "model-api-key").exists())

    def test_admin_trust_and_adb_mode_flow(self):
        root = self.runtime / "admin-state"
        shutil.rmtree(root, ignore_errors=True)
        config = root / "config"
        trust = root / "trusted-app-keys"
        adb_keys = root / "adb_keys"
        env = os.environ.copy()
        env.update({
            "PATH": str(self.bin_dir) + os.pathsep + env["PATH"],
            "AITVBOX_CONFIG_DIR": str(config),
            "AITVBOX_TRUST_ROOT": str(trust),
            "AITVBOX_ADB_KEYS_FILE": str(adb_keys),
            "AITVBOX_NO_RESTART": "1",
        })
        status = run([ADMINCTL, "status"], env=env)
        self.assertEqual(
            status.stdout.strip(),
            b"adb-mode=disabled adb-keys=0 app-keys=0",
        )
        run([ADMINCTL, "set-adb-mode", "development"], env=env)
        self.assertEqual((config / "adb-mode").read_text().strip(), "development")
        run([
            ADMINCTL, "authorize-adb-key",
            str(self.adb_private_key) + ".pub",
        ], env=env)
        self.assertEqual(adb_keys.stat().st_mode & 0o777, 0o600)
        run([
            ADMINCTL, "add-app-key", "developer", self.public_key,
        ], env=env)
        listed = run([ADMINCTL, "list-app-keys"], env=env)
        self.assertIn(b"developer.pem", listed.stdout)
        duplicate = run([
            ADMINCTL, "add-app-key", "developer", self.public_key,
        ], env=env, check=False)
        self.assertNotEqual(duplicate.returncode, 0)
        run([ADMINCTL, "set-adb-mode", "authorized"], env=env)
        status = run([ADMINCTL, "status"], env=env)
        self.assertEqual(
            status.stdout.strip(),
            b"adb-mode=authorized adb-keys=1 app-keys=1",
        )

    def test_hid_report_encoding(self):
        env = os.environ.copy()
        env["AITVBOX_HID_KEYBOARD"] = "/dev/stdout"
        env["AITVBOX_HID_LOCK"] = str(self.runtime / "hid-test.lock")
        key = run([HIDCTL, "key", "4", "2"], env=env)
        self.assertEqual(
            key.stdout,
            b"\x02\x00\x04\x00\x00\x00\x00\x00" + b"\x00" * 8,
        )
        result = run([HIDCTL, "key", "102"], env=env, check=False)
        self.assertNotEqual(result.returncode, 0)
        env["AITVBOX_HID_MOUSE"] = "/dev/stdout"
        mouse = run([HIDCTL, "mouse", "-5", "7", "1", "-1"], env=env)
        self.assertEqual(mouse.stdout, b"\x01\x01\xfb\x07\xff")
        click = run([HIDCTL, "click", "1"], env=env)
        self.assertEqual(
            click.stdout,
            b"\x01\x01\x01\x00\x00" + b"\x01\x00\xff\x00\x00",
        )
        key = run([HIDCTL, "key", "4", "2"], env=env)
        self.assertEqual(
            key.stdout,
            b"\x02\x02\x00\x04\x00\x00\x00\x00\x00" +
            b"\x02\x00\x00\x00\x00\x00\x00\x00\x00",
        )

    def test_kernel_config_injection_updates_incremental_config_and_restores(self):
        sdk = self.runtime / "fake-kernel-sdk"
        shutil.rmtree(sdk, ignore_errors=True)
        (sdk / ".repo").mkdir(parents=True)
        paths = [
            sdk / (
                "device/config/chips/a133/configs/b6/linux-4.9/"
                "openwrt_linux_defconfig"
            ),
            sdk / "kernel/linux-4.9/.config",
        ]
        options = [
            "CONFIG_HIDRAW",
            "CONFIG_INPUT_UINPUT",
            "CONFIG_USB_CONFIGFS_F_HID",
            "CONFIG_SECCOMP",
            "CONFIG_SECCOMP_FILTER",
        ]
        original = "CONFIG_UNRELATED=y\n" + "".join(
            "# {} is not set\n".format(option) for option in options
        )
        for path in paths:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(original, encoding="ascii")

        try:
            run([INJECT_KERNEL_CONFIG, "apply", sdk])
            run([INJECT_KERNEL_CONFIG, "apply", sdk])
            for path in paths:
                content = path.read_text(encoding="ascii")
                for option in options:
                    self.assertEqual(content.count("{}=y".format(option)), 1)
                    self.assertNotIn("# {} is not set".format(option), content)
        finally:
            run([INJECT_KERNEL_CONFIG, "revert", sdk])

        for path in paths:
            self.assertEqual(path.read_text(encoding="ascii"), original)

    def test_product_mode_cleans_all_packages_and_restores_vendor_adbd(self):
        sdk = self.runtime / "fake-sdk"
        shutil.rmtree(sdk, ignore_errors=True)
        (sdk / ".repo").mkdir(parents=True)
        configs = [
            sdk / "openwrt/target/a133/a133-b6/defconfig",
            sdk / "openwrt/openwrt/.config",
            sdk / "out/a133/b6/openwrt/tmp/.config",
        ]
        keys = [
            "CONFIG_PACKAGE_aitvbox-suite",
            "CONFIG_PACKAGE_aitvbox-platform",
            "CONFIG_PACKAGE_aitvbox-usb-hid",
            "CONFIG_PACKAGE_aitvbox-ipkvm",
        ]
        for config in configs:
            config.parent.mkdir(parents=True, exist_ok=True)
            config.write_text(
                "".join("{}=y\n".format(key) for key in keys),
                encoding="ascii",
            )

        vendor_adbd = sdk / "openwrt/package/allwinner/usb/adbd/adbd.init"
        vendor_adbd.parent.mkdir(parents=True)
        write_executable(vendor_adbd, "#!/bin/sh\n# vendor adbd\n")

        openwrt_out = sdk / "out/a133/b6/openwrt"
        roots = [
            openwrt_out / "build_dir/target/root-a133-b6",
            openwrt_out / "build_dir/target/root.orig-a133-b6",
            openwrt_out / "staging_dir/target/root-a133-b6",
        ]
        for root in roots:
            (root / "etc/init.d").mkdir(parents=True)
            (root / "usr/bin").mkdir(parents=True)
            write_executable(
                root / "etc/init.d/adbd",
                "#!/bin/sh\n# product adbd\n",
            )
            write_executable(root / "usr/bin/aitvbox-mcpd", "#!/bin/sh\n")
            for package in ("suite", "platform", "usb-hid", "ipkvm"):
                marker = root / "stamp/.aitvbox-{}_installed".format(package)
                marker.parent.mkdir(parents=True, exist_ok=True)
                marker.touch()

        package_dir = openwrt_out / "extra/packages/aarch64_generic/base"
        package_dir.mkdir(parents=True)
        for package, release in (
            ("suite", 8), ("platform", 3), ("usb-hid", 4), ("ipkvm", 1)
        ):
            (package_dir / "aitvbox-{}_1.0.0-{}_aarch64_generic.ipk".format(
                package, release
            )).touch()
            (openwrt_out / "build_dir/target/aitvbox-{}-1.0.0".format(
                package
            )).mkdir(parents=True)

        run([TOGGLE_PRODUCT, "off", sdk])
        for config in configs:
            content = config.read_text(encoding="ascii")
            for key in keys:
                self.assertIn("# {} is not set".format(key), content)
        self.assertEqual(list(package_dir.glob("aitvbox-*.ipk")), [])
        for root in roots:
            self.assertFalse((root / "usr/bin/aitvbox-mcpd").exists())
            self.assertEqual(
                (root / "etc/init.d/adbd").read_bytes(),
                vendor_adbd.read_bytes(),
            )

        run([TOGGLE_PRODUCT, "on", sdk])
        for config in configs:
            content = config.read_text(encoding="ascii")
            for key in keys:
                self.assertIn("{}=y".format(key), content)

    def test_sdk_package_sync_is_an_exact_mirror(self):
        sdk = self.runtime / "fake-sync-sdk"
        shutil.rmtree(sdk, ignore_errors=True)
        (sdk / ".repo").mkdir(parents=True)
        stale = (
            sdk / "openwrt/package/allwinner/custom/"
            "aitvbox-platform/files/removed-init"
        )
        stale.parent.mkdir(parents=True)
        stale.write_text("stale\n", encoding="ascii")

        run([SYNC_TO_SDK, "--allow-overwrite", sdk])
        self.assertFalse(stale.exists())
        for package in ("suite", "platform", "usb-hid", "ipkvm"):
            makefile = (
                sdk / "openwrt/package/allwinner/custom" /
                "aitvbox-{}".format(package) / "Makefile"
            )
            self.assertTrue(makefile.is_file())


if __name__ == "__main__":
    unittest.main(verbosity=2)
