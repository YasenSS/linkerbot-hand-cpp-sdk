"""Configure-only regression cases for transport selection; no downloads needed."""
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TARGETS = '''
add_library(linkerbot::comm INTERFACE IMPORTED GLOBAL)
set_target_properties(linkerbot::comm PROPERTIES LINKERBOT_COMM_VERSION 0.3.2)
add_library(linkerbot::comm-compat INTERFACE IMPORTED GLOBAL)
'''


class TransportProviderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.source = self.root / 'transport-source'
        self.source.mkdir()
        (self.source / 'CMakeLists.txt').write_text(TARGETS, encoding='utf-8')
        self.prefix = self.root / 'transport-prefix'
        self.config = self.prefix / 'lib/cmake/linkerbot-comm'
        self.config.mkdir(parents=True)
        (self.config / 'linkerbot-comm-config.cmake').write_text(TARGETS, encoding='utf-8')
        (self.config / 'linkerbot-comm-config-version.cmake').write_text(
            'set(PACKAGE_VERSION 0.3.2)\nset(PACKAGE_VERSION_COMPATIBLE TRUE)\n', encoding='utf-8')

    def configure(self, options, expected, success=True):
        result = subprocess.run([
            'cmake', '-S', str(ROOT), '-B', str(self.root / 'build'),
            '-DLINKERBOT_BUILD_TESTS=OFF', '-DLINKERBOT_BUILD_TOOLS=OFF',
            '-DLINKERBOT_BUILD_EXAMPLES=OFF', '-DLINKERBOT_ALLOW_DEPENDENCY_DOWNLOADS=OFF',
            *options,
        ], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode == 0, success, result.stdout)
        self.assertIn(expected, result.stdout)

    def test_explicit_source(self):
        self.configure(['-DLINKERBOT_COMM_PROVIDER=SOURCE',
                        f'-DLINKERBOT_COMM_SOURCE_DIR={self.source}'], 'Transport: SOURCE')

    @unittest.skipUnless(os.name == 'nt', 'Windows path syntax')
    def test_untyped_windows_source_path(self):
        self.configure(['-DLINKERBOT_COMM_PROVIDER=SOURCE',
                        f'-DLINKERBOT_COMM_SOURCE_DIR={self.source}'],
                       'Transport: SOURCE (' + self.source.as_posix())

    def test_legacy_source(self):
        self.configure([f'-DLINKERBOT_COMM_SOURCE_DIR={self.source}'], 'Transport: SOURCE')

    def test_prebuilt_ignores_invalid_source(self):
        self.configure(['-DLINKERBOT_COMM_PROVIDER=PREBUILT',
                        '-DLINKERBOT_COMM_SOURCE_DIR=/missing',
                        f'-DLINKERBOT_COMM_PREBUILT_PREFIX={self.prefix}'], 'Transport: PREBUILT')

    def test_auto_prefix_takes_priority(self):
        self.configure([f'-DLINKERBOT_COMM_SOURCE_DIR={self.source}',
                        f'-DLINKERBOT_COMM_PREBUILT_PREFIX={self.prefix}'], 'Transport: PREBUILT')

    def test_legacy_empty_source(self):
        self.configure(['-DLINKERBOT_COMM_SOURCE_DIR=',
                        f'-DCMAKE_PREFIX_PATH={self.prefix}'], 'Transport: PREBUILT')

    def test_prebuilt_standard_search(self):
        self.configure(['-DLINKERBOT_COMM_PROVIDER=PREBUILT',
                        f'-DCMAKE_PREFIX_PATH={self.prefix}'], 'Transport: PREBUILT')

    def test_source_does_not_fall_back_to_package(self):
        self.configure(['-DLINKERBOT_COMM_PROVIDER=SOURCE',
                        '-DLINKERBOT_COMM_SOURCE_DIR=/missing',
                        f'-DCMAKE_PREFIX_PATH={self.prefix}'], 'SOURCE requires', False)

    def test_missing_prefix_does_not_fall_back(self):
        self.configure(['-DLINKERBOT_COMM_PROVIDER=PREBUILT',
                        f'-DLINKERBOT_COMM_PREBUILT_PREFIX={self.root / "missing"}',
                        f'-DCMAKE_PREFIX_PATH={self.prefix}'],
                       'Could not find a package configuration file', False)

    def test_reject_cached_package_outside_prefix(self):
        self.configure(['-DLINKERBOT_COMM_PROVIDER=PREBUILT',
                        f'-Dlinkerbot-comm_DIR={self.config}',
                        f'-DLINKERBOT_COMM_PREBUILT_PREFIX={self.root / "other"}'],
                       'outside LINKERBOT_COMM_PREBUILT_PREFIX', False)

    def test_invalid_provider(self):
        self.configure(['-DLINKERBOT_COMM_PROVIDER=invalid'],
                       'Invalid LINKERBOT_COMM_PROVIDER', False)


if __name__ == '__main__':
    unittest.main()
