"""Configure-only regression cases for transport selection; no downloads needed."""
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TARGETS = '''
add_library(linkerbot::comm INTERFACE IMPORTED GLOBAL)
set_target_properties(linkerbot::comm PROPERTIES LINKERBOT_COMM_VERSION {version})
add_library(linkerbot::comm-compat INTERFACE IMPORTED GLOBAL)
'''


class TransportProviderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.source = self.root / 'transport-source'
        self.source.mkdir()
        (self.source / 'CMakeLists.txt').write_text(TARGETS.format(version='0.3.4'), encoding='utf-8')
        self.prefix = self.root / 'transport-prefix'
        self.config = self.prefix / 'lib/cmake/linkerbot-comm'
        self.config.mkdir(parents=True)
        self.package_version('0.3.4')
        self.configurations = 0

    def package_version(self, version):
        (self.config / 'linkerbot-comm-config.cmake').write_text(
            TARGETS.format(version=version), encoding='utf-8')
        script = self.root / 'version.cmake'
        script.write_text(
            'include(CMakePackageConfigHelpers)\n'
            f'write_basic_package_version_file("{self.config.as_posix()}/linkerbot-comm-config-version.cmake" '
            f'VERSION {version} COMPATIBILITY SameMinorVersion)\n', encoding='utf-8')
        subprocess.run(['cmake', '-P', str(script)], check=True, capture_output=True, text=True)

    def configure(self, options, expected, success=True):
        self.configurations += 1
        self.build = self.root / f'build-{self.configurations}'
        result = subprocess.run([
            'cmake', '-S', str(ROOT), '-B', str(self.build),
            '-DLINKERBOT_BUILD_TESTS=OFF', '-DLINKERBOT_BUILD_TOOLS=OFF',
            '-DLINKERBOT_BUILD_EXAMPLES=OFF', '-DLINKERBOT_ALLOW_DEPENDENCY_DOWNLOADS=OFF',
            *options,
        ], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode == 0, success, result.stdout)
        self.assertIn(expected, result.stdout)

    def test_explicit_source(self):
        self.configure(['-DLINKERBOT_COMM_PROVIDER=SOURCE',
                        f'-DLINKERBOT_COMM_SOURCE_DIR={self.source}'], 'Transport: SOURCE')

    def test_source_version_floor(self):
        for version in ['0.3.0', '0.3.1', '0.3.2', '0.3.3', '0.3.4', '0.3.5', '0.4.0']:
            with self.subTest(version=version):
                (self.source / 'CMakeLists.txt').write_text(TARGETS.format(version=version))
                valid = version in ['0.3.4', '0.3.5']
                self.configure(['-DLINKERBOT_COMM_PROVIDER=SOURCE',
                                f'-DLINKERBOT_COMM_SOURCE_DIR={self.source}'],
                               'Transport: SOURCE' if valid else 'requires linkerbot-comm', valid)

    def test_prebuilt_version_floor(self):
        for version in ['0.3.0', '0.3.1', '0.3.2', '0.3.3', '0.3.4', '0.3.5', '0.4.0']:
            with self.subTest(version=version):
                self.package_version(version)
                valid = version in ['0.3.4', '0.3.5']
                self.configure(['-DLINKERBOT_COMM_PROVIDER=PREBUILT',
                                f'-DLINKERBOT_COMM_PREBUILT_PREFIX={self.prefix}'],
                               'Transport: PREBUILT' if valid else 'compatible', valid)

    def test_parent_target_version_floor(self):
        for version in ['0.3.3', '0.3.4', '0.3.5', '0.4.0']:
            with self.subTest(version=version):
                parent = self.root / ('parent-' + version)
                parent.mkdir()
                (parent / 'CMakeLists.txt').write_text(
                    'cmake_minimum_required(VERSION 3.16)\nproject(parent LANGUAGES CXX)\n'
                    + TARGETS.format(version=version)
                    + f'add_subdirectory("{ROOT.as_posix()}" hand)\n')
                result = subprocess.run([
                    'cmake', '-S', str(parent), '-B', str(parent / 'build'),
                    '-DLINKERBOT_BUILD_TESTS=OFF', '-DLINKERBOT_BUILD_TOOLS=OFF',
                    '-DLINKERBOT_BUILD_EXAMPLES=OFF', '-DLINKERBOT_ALLOW_DEPENDENCY_DOWNLOADS=OFF',
                ], capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, version in ['0.3.4', '0.3.5'],
                                 result.stdout + result.stderr)

    @unittest.skipUnless(os.name == 'nt', 'Windows path syntax')
    def test_untyped_windows_source_path(self):
        # Reparse a generated path as the real fixture setup does. This catches
        # CMake escape errors without depending on Windows short/long names.
        with (self.source / 'CMakeLists.txt').open('a', encoding='utf-8') as output:
            output.write(r'''
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/fixture-path.cmake"
    "set(fixture \"${LINKERBOT_COMM_SOURCE_DIR}/tests/pcan_fixture.cpp\")\n")
include("${CMAKE_CURRENT_BINARY_DIR}/fixture-path.cmake")
''')
        self.configure(['-DLINKERBOT_COMM_PROVIDER=SOURCE',
                        f'-DLINKERBOT_COMM_SOURCE_DIR={self.source}'],
                       'Transport: SOURCE')
        generated = (self.build / '_deps/linkerbot-transport-cpp/fixture-path.cmake').read_text()
        self.assertNotIn('\\', generated)

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

    def forwarding_package(self):
        canonical = self.prefix / 'lib/cmake/linkerbot-transport'
        canonical.mkdir(parents=True)
        (canonical / 'linkerbot-transport-config.cmake').write_text(TARGETS.format(version='0.3.4'), encoding='utf-8')
        (self.config / 'linkerbot-comm-config.cmake').write_text(
            'include(CMakeFindDependencyMacro)\n'
            'find_dependency(linkerbot-transport CONFIG)\n', encoding='utf-8')
        return canonical

    def test_prebuilt_forwarding_package_uses_requested_prefix(self):
        self.forwarding_package()
        self.configure(['-DLINKERBOT_COMM_PROVIDER=PREBUILT',
                        '-DLINKERBOT_COMM_SOURCE_DIR=/missing',
                        f'-DLINKERBOT_COMM_PREBUILT_PREFIX={self.prefix}'], 'Transport: PREBUILT')

    def test_reject_cached_forwarded_package_outside_prefix(self):
        canonical = self.forwarding_package()
        outside = self.root / 'outside-transport'
        outside.mkdir()
        (outside / 'linkerbot-transport-config.cmake').write_text(
            (canonical / 'linkerbot-transport-config.cmake').read_text(), encoding='utf-8')
        self.configure(['-DLINKERBOT_COMM_PROVIDER=PREBUILT',
                        f'-DLINKERBOT_COMM_PREBUILT_PREFIX={self.prefix}',
                        f'-Dlinkerbot-transport_DIR={outside}'],
                       'outside LINKERBOT_COMM_PREBUILT_PREFIX', False)

    def test_invalid_provider(self):
        self.configure(['-DLINKERBOT_COMM_PROVIDER=invalid'],
                       'Invalid LINKERBOT_COMM_PROVIDER', False)


if __name__ == '__main__':
    unittest.main()
