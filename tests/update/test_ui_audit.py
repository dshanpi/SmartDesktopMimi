"""Input-language exceptions must not weaken the shipping UI copy gate."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("ui_audit", ROOT / "scripts/audit_english_ui.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class BilingualGate(unittest.TestCase):
    helper = 'static const char *tr(const char *zh, const char *en) { return ui_i18n_is_zh() ? zh : en; }\n' + '\n' * 30
    def check_copy(self, filename, source):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            src = root / "apps/lv_port_linux/src"
            header = src / "ui/locale_en_us.h"
            header.parent.mkdir(parents=True)
            header.write_text('#define UI_LOCALE_ID             "en-US"\n', encoding="utf-8")
            target = src / filename
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(source, encoding="utf-8")
            with patch.object(audit, "REPO", root), patch.object(audit, "SOURCE_ROOT", src), \
                    contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                return audit.main()

    def test_real_parser_vocabulary(self):
        text = (ROOT / "apps/lv_port_linux/src/apps/voice/voice_command.c").read_text(encoding="utf-8")
        self.assertEqual(self.check_copy("apps/voice/voice_command.c", text), 0)

    def test_new_untranslated_text_in_parser_is_rejected(self):
        self.assertEqual(self.check_copy("apps/voice/voice_command.c", 'char *s="未翻译的新按钮";'), 1)

    def test_input_phrase_is_not_exempt_in_ui(self):
        self.assertEqual(self.check_copy("apps/voice/voice_app.c", 'lv_label_set_text(label,"打开设置");'), 1)

    def test_new_command_requires_review(self):
        self.assertEqual(self.check_copy("apps/voice/voice_command.c", 'char *s="删除所有文件";'), 1)

    def test_bilingual_ui_remains_valid(self):
        self.assertEqual(self.check_copy("apps/voice/voice_app.c", 'char *s=ui_i18n_is_zh() ? "打开设置" : "Open settings";'), 0)

    def test_comments_are_not_ui_copy(self):
        self.assertEqual(self.check_copy("apps/voice/voice_app.c", '/* 中文注释 */ char *s="Settings";'), 0)

    def test_verified_helper_multiline_pair(self):
        self.assertEqual(self.check_copy("apps/iot/iot_app.c", self.helper + 'label(tr("刷新设备",\n "Refresh devices"));'), 0)

    def test_helper_does_not_exempt_neighbor_on_same_line(self):
        self.assertEqual(self.check_copy("apps/iot/iot_app.c", self.helper + 'label(tr("刷新设备", "Refresh devices")); label("刷新设备");'), 1)

    def test_helper_requires_english_literal(self):
        for expression in ['tr("刷新设备", "")', 'tr("刷新设备", "刷新 devices")', 'tr("刷新设备", variable)']:
            with self.subTest(expression=expression):
                self.assertEqual(self.check_copy("apps/iot/iot_app.c", self.helper + expression), 1)

    def test_unverified_or_commented_helper_rejected(self):
        for helper in ['', self.helper.replace('zh : en', 'en : zh'), '/*' + self.helper + '*/']:
            with self.subTest(helper=helper):
                self.assertEqual(self.check_copy("apps/iot/iot_app.c", helper + 'label(tr("刷新设备", "Refresh devices"));'), 1)

    def test_real_feature_screens(self):
        for name in ['iot/iot_app.c', 'voice/voice_app.c', 'pairing/pairing_app.c']:
            with self.subTest(name=name):
                source = (ROOT / 'apps/lv_port_linux/src/apps' / name).read_text(encoding='utf-8')
                self.assertEqual(self.check_copy('apps/' + name, source), 0)


if __name__ == "__main__":
    unittest.main()
