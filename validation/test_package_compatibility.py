"""Run the package compatibility matrix with the current SDK patch version."""
import pathlib
import subprocess
import tempfile
import unittest

from release_package import project_version


ROOT = pathlib.Path(__file__).resolve().parents[1]


class PackageCompatibilityTests(unittest.TestCase):
    def test_default_version_tracks_current_source(self):
        version = project_version(ROOT)
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            prefix = root / 'prefix'
            hand = prefix / 'lib/cmake/linkerbot-hand-cpp'
            comm = prefix / 'lib/cmake/linkerbot-comm'
            hand.mkdir(parents=True)
            comm.mkdir(parents=True)
            (hand / 'linkerbot-hand-cpp-config.cmake').write_text(
                'include(CMakeFindDependencyMacro)\n'
                f'find_dependency(linkerbot-comm {version} CONFIG)\n')
            (comm / 'linkerbot-comm-config.cmake').write_text('')
            generate = root / 'versions.cmake'
            generate.write_text('include(CMakePackageConfigHelpers)\n' + ''.join(
                f'write_basic_package_version_file("{directory.as_posix()}/{name}-config-version.cmake" '
                f'VERSION {version} COMPATIBILITY SameMinorVersion)\n'
                for directory, name in [(hand, 'linkerbot-hand-cpp'), (comm, 'linkerbot-comm')]))
            subprocess.run(['cmake', '-P', str(generate)], check=True, capture_output=True, text=True)
            result = subprocess.run([
                'cmake', f'-DHAND_PREFIX={prefix}', '-P',
                str(ROOT / 'validation/check_package_compatibility.cmake'),
            ], cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for name in ['matching', 'newer-patch', 'newer-minor', 'older-minor', 'missing-comm',
                         'source-matching', 'source-newer-patch', 'source-newer-minor', 'source-older-minor']:
                self.assertIn(f'{name}: expected configure result observed', result.stdout)


if __name__ == '__main__':
    unittest.main()
