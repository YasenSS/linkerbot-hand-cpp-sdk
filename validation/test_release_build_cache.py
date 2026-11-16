"""Check shared Catch2 cache reuse and rejection of incompatible installations."""
import argparse
import contextlib
import io
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from prepare_catch2 import prepare, REVISION
from build_support import add_build_arguments, cmake_options, validation_options


class CompilerCacheOptionsTests(unittest.TestCase):
    def parse(self, arguments):
        parser = argparse.ArgumentParser()
        add_build_arguments(parser)
        return parser.parse_args(arguments)

    def test_cache_and_debug_policy_are_forwarded_to_child_validators(self):
        args = self.parse(['--config', 'Debug', '--generator', 'Ninja', '--compiler-cache', 'sccache'])
        options = cmake_options(args)
        self.assertIn('-DCMAKE_CXX_COMPILER_LAUNCHER=sccache', options)
        self.assertIn('-DCMAKE_POLICY_DEFAULT_CMP0141=NEW', options)
        self.assertIn('-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>', options)
        self.assertEqual(cmake_options(self.parse(validation_options(args))), options)

    def test_visual_studio_cannot_silently_ignore_the_launcher(self):
        args = self.parse(['--generator', 'Visual Studio 17 2022', '--compiler-cache', 'sccache'])
        with self.assertRaisesRegex(ValueError, 'Ninja or Makefile'):
            cmake_options(args)

    def test_existing_builds_do_not_gain_cache_or_debug_flags(self):
        options = cmake_options(self.parse(['--generator', 'Visual Studio 17 2022']))
        self.assertFalse(any('LAUNCHER' in option or 'MSVC_DEBUG' in option or 'CMP0141' in option
                             for option in options))

    def test_linux_ccache_is_forwarded_to_child_validators(self):
        args = self.parse(['--generator', 'Ninja', '--compiler-cache', 'ccache'])
        options = cmake_options(args)
        self.assertIn('-DCMAKE_CXX_COMPILER_LAUNCHER=ccache', options)
        self.assertEqual(cmake_options(self.parse(validation_options(args))), options)


class Catch2CacheTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.prefix = self.root / 'prefix'
        self.config = self.prefix / 'lib/cmake/Catch2/Catch2Config.cmake'
        self.environment = patch.dict(os.environ, {
            'CMAKE_GENERATOR': 'Ninja', 'VCToolsVersion': '14.44.35207',
            'WindowsSDKVersion': '10.0.26100.0', 'VSCMD_ARG_TGT_ARCH': 'arm64',
        })
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def cold_install(self):
        def run(command, **kwargs):
            if command[:2] == ['cmake', '--install']:
                self.config.parent.mkdir(parents=True)
                self.config.write_text('# installed package\n', encoding='utf-8')
            return subprocess.CompletedProcess(command, 0)

        with patch('prepare_catch2.subprocess.run', side_effect=run) as execute, \
                patch('prepare_catch2.subprocess.check_output', return_value=REVISION + '\n'):
            result = prepare(self.prefix, self.root / 'work', 'Release', 4)
        return result, [call.args[0] for call in execute.call_args_list]

    def test_one_install_is_reused_without_git_or_compiler_invocations(self):
        result, commands = self.cold_install()
        self.assertEqual(result, self.config.parent)
        self.assertEqual(sum(c[:2] == ['cmake', '--build'] for c in commands), 1)
        configure = next(c for c in commands if c[:2] == ['cmake', '-S'])
        self.assertIn('-DCMAKE_CXX_STANDARD=17', configure)
        self.assertIn('-DCMAKE_POSITION_INDEPENDENT_CODE=ON', configure)
        self.assertIn('-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL', configure)
        with patch('prepare_catch2.subprocess.run') as execute, \
                patch('prepare_catch2.subprocess.check_output') as git, \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(prepare(self.prefix, self.root / 'other-work', 'Release', 4), result)
        execute.assert_not_called()
        git.assert_not_called()

    def test_configuration_architecture_compiler_and_sdk_cannot_share_a_prefix(self):
        self.cold_install()
        changes = [{'VSCMD_ARG_TGT_ARCH': 'x64'}, {'VCToolsVersion': '14.45.1'},
                   {'WindowsSDKVersion': '10.0.99999.0'}, {'CMAKE_GENERATOR': 'Visual Studio 17 2022'}]
        for environment in changes:
            with self.subTest(environment=environment), patch.dict(os.environ, environment), \
                    self.assertRaisesRegex(RuntimeError, 'incompatible'):
                prepare(self.prefix, self.root / 'work', 'Release', 4)
        with self.assertRaisesRegex(RuntimeError, 'incompatible'):
            prepare(self.prefix, self.root / 'work', 'Debug', 4)

    def test_missing_package_does_not_count_as_a_cache_hit(self):
        self.cold_install()
        self.config.unlink()
        with self.assertRaisesRegex(RuntimeError, 'incomplete'):
            prepare(self.prefix, self.root / 'work', 'Release', 4)

    def test_changed_compiler_cache_options_invalidate_the_installation(self):
        self.cold_install()
        with self.assertRaisesRegex(RuntimeError, 'incompatible'):
            prepare(self.prefix, self.root / 'work', 'Release', 4, compiler_cache='sccache')

    def test_linux_toolchain_fingerprint_cannot_share_an_installation(self):
        self.cold_install()
        with patch.dict(os.environ, {'LINKERBOT_CI_TOOLCHAIN': 'different-compiler-and-sysroot'}), \
                self.assertRaisesRegex(RuntimeError, 'incompatible'):
            prepare(self.prefix, self.root / 'work', 'Release', 4)

    def test_changed_upstream_tag_fails_before_building(self):
        with patch('prepare_catch2.subprocess.run') as execute, \
                patch('prepare_catch2.subprocess.check_output', return_value='a' * 40), \
                self.assertRaisesRegex(RuntimeError, 'pinned source'):
            prepare(self.prefix, self.root / 'work', 'Release', 4)
        self.assertEqual(execute.call_count, 1)  # Clone only; no configure/build/install.
        self.assertFalse((self.prefix / 'ci-identity.json').exists())


if __name__ == '__main__':
    unittest.main()
