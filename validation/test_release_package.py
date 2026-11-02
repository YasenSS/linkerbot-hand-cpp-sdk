"""Regression checks for the release gate, using real ZIP and TGZ archives."""
import hashlib
import io
import json
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

from release_package import audit_package, project_version, unpack_archive


class ReleasePackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.prefix = self.root / 'staging' / 'sdk-0.3.0'
        self.sources = {}
        for key, package, library in [('comm', 'linkerbot-comm', 'linkerbot-comm'),
                                      ('hand', 'linkerbot-hand-cpp', 'linkerbot-cpp-sdk')]:
            source = self.root / (key + '-source')
            source.mkdir()
            (source / 'CMakeLists.txt').write_text(f'project({package} VERSION 0.3.0 LANGUAGES CXX)')
            self.sources[key] = {'path': source, 'sha': ('a' if key == 'hand' else 'b') * 40,
                                 'version': project_version(source)}
            for name in ['LICENSE', 'NOTICE', 'README.md', 'CHANGELOG.md', 'CONTRIBUTING.md']:
                (source / name).write_text(key + ':' + name)
                self.write(f'share/{package}/{name}', key + ':' + name)
            metadata = {'version': '0.3.0', 'configuration': 'Release', 'system': 'Linux',
                        'comm_sha': 'b' * 40, 'hand_sha': 'a' * 40, 'source_dirty': False}
            self.write(f'share/{package}/build-info.json', json.dumps(metadata))
            doc = 'docs/zh/windows-build.md' if key == 'comm' else 'docs/zh/guides/windows.md'
            self.write(f'share/{package}/{doc}', 'build instructions')
            if key == 'hand':
                (source / 'README.en.md').write_text('English quick start')
                self.write(f'share/{package}/README.en.md', 'English quick start')
                self.write(f'share/{package}/docs/zh/guides/usage.md', 'API usage instructions')
                self.write(f'share/{package}/docs/en/guides/windows.md', 'English build instructions')
                self.write(f'share/{package}/docs/en/guides/usage.md', 'English API usage instructions')
            for suffix in ['-config.cmake', '-config-version.cmake']:
                self.write(f'lib/cmake/{package}/{package}{suffix}', '# relocatable')
            self.write(f'lib/lib{library}.a', 'static library placeholder')
        self.write('include/linkerbot/can/pcan_transport.hpp', '// public')
        self.write('include/linkerbot/can/slcan_transport.hpp', '// public')
        self.write('include/linkerbot/hand/adapter_transport.hpp', '// public')
        self.write('share/linkerbot-comm/docs/zh/slcan.md', 'SLCAN instructions')

    def write(self, relative, contents):
        path = self.prefix / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents)

    def archive(self, format):
        archive = self.root / ('sdk.zip' if format == 'ZIP' else 'sdk.tar.gz')
        if format == 'ZIP':
            with zipfile.ZipFile(archive, 'w') as packed:
                for path in self.prefix.rglob('*'):
                    if path.is_file():
                        packed.write(path, path.relative_to(self.prefix.parent))
        else:
            with tarfile.open(archive, 'w:gz') as packed:
                packed.add(self.prefix, arcname=self.prefix.name)
        self.checksum(archive)
        return archive

    def checksum(self, archive):
        archive.with_name(archive.name + '.sha256').write_text(
            hashlib.sha256(archive.read_bytes()).hexdigest() + '  ' + archive.name + '\n')

    def audit(self, format):
        archive = self.archive(format)
        prefix, _ = unpack_archive(archive, self.root / ('解压 SDK with spaces ' + format))
        return audit_package(prefix, archive, 'hand', self.sources, 'Release', 'linux',
                             require_clean=True, require_slcan=True,
                             forbidden_paths=[self.sources['hand']['path'], self.sources['comm']['path']])

    def test_zip_and_tgz_pass_the_same_gate(self):
        self.assertEqual(self.audit('ZIP'), self.audit('TGZ'))

    def test_tampered_checksum_is_rejected_for_both_formats(self):
        for format in ['ZIP', 'TGZ']:
            with self.subTest(format=format):
                archive = self.archive(format)
                archive.with_name(archive.name + '.sha256').write_text('0' * 64)
                with self.assertRaisesRegex(RuntimeError, 'SHA256'):
                    unpack_archive(archive, self.root / format)

    def test_wrong_version_or_dependency_commit_is_rejected(self):
        path = self.prefix / 'share/linkerbot-hand-cpp/build-info.json'
        original = json.loads(path.read_text())
        for key, value in [('version', '0.2.0'), ('comm_sha', 'c' * 40)]:
            with self.subTest(key=key):
                path.write_text(json.dumps({**original, key: value}))
                with self.assertRaisesRegex(RuntimeError, 'version/provenance'):
                    audit_package(self.prefix, Path('sdk.zip'), 'hand', self.sources, 'Release', 'linux')

    def test_changed_license_and_missing_notice_are_rejected(self):
        for name in ['LICENSE', 'NOTICE']:
            with self.subTest(name=name):
                path = self.prefix / 'share/linkerbot-comm' / name
                original = path.read_text()
                if name == 'LICENSE':
                    path.write_text('wrong license')
                else:
                    path.unlink()
                with self.assertRaises(RuntimeError):
                    audit_package(self.prefix, Path('sdk.zip'), 'hand', self.sources, 'Release', 'linux')
                path.write_text(original)

    def test_vendor_dll_and_source_paths_are_rejected(self):
        for relative, contents in [('bin/HCanbus.dll', 'vendor'),
                                   ('lib/cmake/leak.cmake', str(self.sources['hand']['path']))]:
            with self.subTest(relative=relative):
                self.write(relative, contents)
                with self.assertRaisesRegex(RuntimeError, 'leaked'):
                    audit_package(self.prefix, Path('sdk.zip'), 'hand', self.sources, 'Release', 'linux',
                                  forbidden_paths=[self.sources['hand']['path']])
                (self.prefix / relative).unlink()

    def test_missing_communication_library_is_rejected(self):
        (self.prefix / 'lib/liblinkerbot-comm.a').unlink()
        with self.assertRaisesRegex(RuntimeError, 'Library missing'):
            self.audit('TGZ')

    def test_missing_public_guide_is_rejected(self):
        for guide in ['zh/guides/windows.md', 'zh/guides/usage.md',
                      'en/guides/windows.md', 'en/guides/usage.md']:
            with self.subTest(guide=guide):
                path = self.prefix / 'share/linkerbot-hand-cpp/docs' / guide
                contents = path.read_text()
                path.unlink()
                with self.assertRaisesRegex(RuntimeError, 'Required package entry missing'):
                    audit_package(self.prefix, Path('sdk.zip'), 'hand', self.sources, 'Release', 'linux')
                path.write_text(contents)

    def test_missing_english_readme_is_rejected(self):
        (self.prefix / 'share/linkerbot-hand-cpp/README.en.md').unlink()
        with self.assertRaisesRegex(RuntimeError, 'Installed document differs from source'):
            audit_package(self.prefix, Path('sdk.zip'), 'hand', self.sources, 'Release', 'linux')

    def test_dirty_package_cannot_pass_release_gate(self):
        path = self.prefix / 'share/linkerbot-comm/build-info.json'
        metadata = json.loads(path.read_text())
        path.write_text(json.dumps({**metadata, 'source_dirty': True}))
        with self.assertRaisesRegex(RuntimeError, 'dirty source'):
            self.audit('ZIP')

    def test_windows_requires_matching_runtime_and_msvc_libraries(self):
        archive = Path('sdk-Windows-x64-msvc143-19.44-Release-MD.zip')
        for package in ['linkerbot-comm', 'linkerbot-hand-cpp']:
            path = self.prefix / 'share' / package / 'build-info.json'
            metadata = json.loads(path.read_text())
            metadata.update(system='Windows', runtime='MD', architecture='x64', toolset='143')
            path.write_text(json.dumps(metadata))
        for library in ['linkerbot-comm', 'linkerbot-cpp-sdk']:
            (self.prefix / 'lib' / ('lib' + library + '.a')).rename(self.prefix / 'lib' / (library + '.lib'))
        audit_package(self.prefix, archive, 'hand', self.sources, 'Release', 'win32', require_clean=True)
        path = self.prefix / 'share/linkerbot-comm/build-info.json'
        metadata = json.loads(path.read_text())
        path.write_text(json.dumps({**metadata, 'runtime': 'MDd'}))
        with self.assertRaisesRegex(RuntimeError, 'runtime/architecture/toolset'):
            audit_package(self.prefix, archive, 'hand', self.sources, 'Release', 'win32')

    def test_archive_paths_cannot_escape_extraction_directory(self):
        for format in ['ZIP', 'TGZ']:
            with self.subTest(format=format):
                archive = self.root / ('bad.zip' if format == 'ZIP' else 'bad.tar.gz')
                if format == 'ZIP':
                    with zipfile.ZipFile(archive, 'w') as packed:
                        packed.writestr('../escaped.txt', 'invalid')
                else:
                    with tarfile.open(archive, 'w:gz') as packed:
                        member = tarfile.TarInfo('../escaped.txt')
                        member.size = 7
                        packed.addfile(member, io.BytesIO(b'invalid'))
                self.checksum(archive)
                with self.assertRaisesRegex(RuntimeError, 'Invalid archive path'):
                    unpack_archive(archive, self.root / format)
                self.assertFalse((self.root / 'escaped.txt').exists())


if __name__ == '__main__':
    unittest.main()
