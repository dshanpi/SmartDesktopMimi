"""Missing SDK is not_run; invalid configured SDK must fail rather than skip."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from tests.sdk_source import provider_source, services_source, capture_source

class SdkSourceTests(unittest.TestCase):
    def test_unconfigured_platform_check_is_explicitly_not_run(self):
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(unittest.SkipTest, 'not_run'):
                provider_source('anything.c')

    def test_configured_missing_or_changed_recipe_is_failure(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            with patch.dict(os.environ, {'MIMI_A133_PROVIDER_ROOT': str(root)}):
                with self.assertRaisesRegex(ValueError, "component root"):
                    provider_source('anything.c')
                (root / 'private').mkdir()
                (root / 'private/provider-recipe.json').write_text('{}')
                with self.assertRaisesRegex(ValueError, 'component root'):
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


class CaptureSdkSourceTests(unittest.TestCase):
    def test_unconfigured_capture_is_not_run(self):
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(unittest.SkipTest, 'not_run'):
                capture_source('main.cpp')

    def test_changed_capture_lock_is_failure(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            (root / 'source-lock.json').write_text('{}')
            with patch.dict(os.environ, {'MIMI_A133_CAPTURE_ROOT': str(root)}):
                with self.assertRaisesRegex(ValueError, 'lock changed'):
                    capture_source('main.cpp')

class SelectedProviderSourceTests(unittest.TestCase):
    def test_selected_runtime_not_legacy_and_changed_input_rejected(self):
        from tests.update.test_product_provider import ProductProviderTests, p
        import tests.sdk_source as source
        with tempfile.TemporaryDirectory() as directory:
            sdk,component,recipe,pins,tool=ProductProviderTests().fixture(Path(directory).resolve())
            legacy=component/'public/code'/p.AI_SOURCE;legacy.parent.mkdir(parents=True,exist_ok=True);legacy.write_text('obsolete')
            with patch.object(source,'provider',p), patch.object(p,'RECIPES',pins), patch.object(p,'TOOL_HASH',tool), patch.dict(os.environ,{'MIMI_A133_PROVIDER_ROOT':str(component)},clear=True):
                chosen=source.provider_source(p.AI_SOURCE,'public')
                self.assertEqual(chosen,component/p.EDITION/'code'/p.AI_SOURCE)
                chosen.write_text('modified')
                with self.assertRaises(ValueError):source.provider_source(p.AI_SOURCE,'public')
    def test_private_component_not_historical_sdk_copy(self):
        from tests.update.test_product_provider import ProductProviderTests, p
        import tests.sdk_source as source
        with tempfile.TemporaryDirectory() as directory:
            sdk,component,recipe,pins,tool,external,product,lock=ProductProviderTests().private_fixture(Path(directory).resolve())
            env={'MIMI_A133_PROVIDER_ROOT':str(component),'MIMI_A133_PRIVATE_SDK':str(external),'MIMI_PRIVATE_PRODUCT_ROOT':str(product)}
            with patch.object(source,'provider',p), patch.object(p,'RECIPES',pins), patch.object(p,'TOOL_HASH',tool), patch.object(p,'PRIVATE_LOCK_HASH',p.sha(lock)), patch.object(p,'TUYA_LOCK_HASH',p.sha(product/p.TUYA_LOCK)), patch.dict(os.environ,env,clear=True):
                name='apps/lv_port_linux/src/system/service_cloud.c'
                self.assertEqual(source.provider_source(name),product/p.PRIVATE_SOURCES[name])
