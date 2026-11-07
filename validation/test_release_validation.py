"""Exercise CLI routing without compiling: quick checks must never install/package."""
import contextlib
import io
import json
import runpy
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from release_package import minimum_transport_version


class ValidationRoutingTests(unittest.TestCase):
    def run_validation(self, suite='all', quick=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            comm = root / 'comm'
            comm.mkdir()
            (comm / 'CMakeLists.txt').write_text(
                f'project(comm VERSION {minimum_transport_version(Path(__file__).resolve().parents[1])})',
                encoding='utf-8')
            work = root / 'work'
            commands = []

            def git(command, **options):
                if 'rev-parse' in command:
                    return 'a' * 40
                return '' if options.get('text') else b''

            def execute(command, **options):
                commands.append(command)
                if '--show-only=json-v1' in command:
                    is_comm = Path(command[command.index('--test-dir') + 1]).name == 'comm-build'
                    if '-R' in command:
                        names = ['pcan-native-loader', 'vendor-native-loader'] if is_comm else ['adapter-native-integration']
                    elif is_comm:
                        names = [f'comm-{index}' for index in range(30)]
                    else:
                        names = ['comm', 'discovery', 'protocol-vectors', 'async-behavior-vectors',
                                 'release-package-audit', 'package-version-compatibility', 'windows_readonly_help', 'windows_readonly_invalid',
                                 'windows_readonly_slcan_fd', 'windows_readonly_slcan_allowlist',
                                 'windows_readonly_slcan_uart', 'windows_readonly_slcan_fd_rate',
                                 'windows_readonly_slcan_profile', 'windows_readonly_slcan_startup',
                                 'l6_hop_readonly_help', 'l6_hop_readonly_invalid']
                    options['stdout'].write(json.dumps({'tests': [{'name': name} for name in names]}) + '\n')
                # Stop full acceptance at its first install, after proving the
                # route is reached. No fake release archives are needed here.
                return subprocess.CompletedProcess(command, 23 if '--install' in command else 0)

            argv = ['run_release_validation.py', '--comm-source', str(comm), '--work-dir', str(work), '--suite', suite]
            if quick:
                argv.append('--quick')
            with patch.object(sys, 'argv', argv), patch('subprocess.check_output', side_effect=git), \
                    patch('subprocess.run', side_effect=execute), patch.dict('os.environ'), \
                    contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as stopped:
                    runpy.run_path(str(Path(__file__).with_name('run_release_validation.py')), run_name='__main__')
            return stopped.exception.code, json.loads((work / 'evidence/summary.json').read_text()), commands

    def test_quick_all_builds_once_then_runs_both_suites(self):
        code, report, commands = self.run_validation(quick=True)
        self.assertEqual(code, 0)
        self.assertTrue(report['quick'])
        self.assertEqual(report['status'], 'passed')
        self.assertEqual(set(report['tests']), {'comm-no-drivers', 'hand-no-drivers',
                                              'comm-native-loader', 'hand-native-loader'})
        builds = [command for command in commands if command[:2] == ['cmake', '--build']]
        self.assertEqual(len(builds), 2)  # One standalone comm build, one paired SDK build.
        self.assertNotIn('example', builds[1])
        self.assertFalse(any('--install' in command or command[0] == 'cpack' for command in commands))
        self.assertNotIn('packages', report)

    def test_full_is_still_the_default(self):
        code, report, commands = self.run_validation()
        self.assertEqual(code, 1)  # Intentional sentinel at the first install.
        self.assertFalse(report['quick'])
        self.assertEqual(report['steps'][-1]['step'], 'comm-install')
        self.assertIn('example', next(command for command in commands if '--target' in command))

    def test_quick_suite_filters_remain_independent(self):
        for suite, expected in [('no-drivers', {'comm-no-drivers', 'hand-no-drivers'}),
                                ('native-loader', {'comm-native-loader', 'hand-native-loader'})]:
            with self.subTest(suite=suite):
                code, report, _ = self.run_validation(suite=suite, quick=True)
                self.assertEqual(code, 0)
                self.assertEqual(set(report['tests']), expected)


if __name__ == '__main__':
    unittest.main()
