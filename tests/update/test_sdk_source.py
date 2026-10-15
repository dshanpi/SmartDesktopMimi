"""Missing SDK is not_run; invalid configured SDK must fail rather than skip."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from tests.sdk_source import provider_source, services_source

class SdkSourceTests(unittest.TestCase):
    def test_unconfigured_platform_check_is_explicitly_not_run(self):
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(unittest.SkipTest, 'not_run'):
                provider_source('anything.c')

    def test_configured_missing_or_changed_recipe_is_failure(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            with patch.dict(os.environ, {'MIMI_A133_PROVIDER_ROOT': str(root)}):
                with self.assertRaises(FileNotFoundError):
                    provider_source('anything.c')
                (root / 'private').mkdir()
                (root / 'private/provider-recipe.json').write_text('{}')
                with self.assertRaisesRegex(ValueError, 'recipe differs'):
                    provider_source('anything.c')


class ServicesSdkSourceTests(unittest.TestCase):
    def test_unconfigured_service_check_is_explicitly_not_run(self):
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(unittest.SkipTest, 'not_run'):
                services_source('src/mcpd.c')

    def test_configured_wrong_service_lock_fails(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            (root / 'source-lock.json').write_text('{}')
            with patch.dict(os.environ, {'MIMI_A133_SERVICES_ROOT': str(root)}):
                with self.assertRaisesRegex(ValueError, 'lock changed'):
                    services_source('src/mcpd.c')
