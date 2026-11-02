"""Shared ZIP/TGZ release checks; does not build or access hardware."""
import hashlib
import json
import re
import tarfile
import zipfile
from pathlib import Path, PurePosixPath


def project_version(source):
    contents = (source / 'CMakeLists.txt').read_text(encoding='utf-8')
    match = re.search(r'\bproject\s*\([^)]*?\bVERSION\s+(\d+\.\d+\.\d+)\b',
                      contents, re.IGNORECASE | re.DOTALL)
    if not match:
        raise RuntimeError(f'Cannot read project version: {source}')
    return match[1]


def unpack_archive(archive, destination):
    """Verify checksum and extract one package root without links or traversal."""
    checksum = archive.with_name(archive.name + '.sha256')
    actual = hashlib.sha256(archive.read_bytes()).hexdigest()
    fields = checksum.read_text(encoding='utf-8').split() if checksum.exists() else []
    if not fields or fields[0].lower() != actual:
        raise RuntimeError(f'Missing or invalid SHA256: {archive.name}')
    if destination.exists():
        raise RuntimeError(f'{destination} already exists; use a fresh work directory')

    def safe_name(name):
        path = PurePosixPath(name.replace('\\', '/'))
        if path.is_absolute() or '..' in path.parts or re.match(r'^[A-Za-z]:', str(path)):
            raise RuntimeError(f'Invalid archive path: {name}')

    if archive.name.endswith('.zip'):
        with zipfile.ZipFile(archive) as packed:
            for member in packed.infolist():
                safe_name(member.filename)
                if (member.external_attr >> 16) & 0o170000 == 0o120000:
                    raise RuntimeError(f'Archive links are not supported: {member.filename}')
            packed.extractall(destination)
    elif archive.name.endswith('.tar.gz'):
        with tarfile.open(archive, 'r:gz') as packed:
            members = packed.getmembers()
            for member in members:
                safe_name(member.name)
                if not (member.isfile() or member.isdir()):
                    raise RuntimeError(f'Archive links/special files are not supported: {member.name}')
            # All entries were checked above; no links or paths outside destination.
            options = {'filter': 'data'} if hasattr(tarfile, 'data_filter') else {}
            packed.extractall(destination, members=members, **options)
    else:
        raise RuntimeError(f'Unsupported release archive: {archive.name}')
    roots = list(destination.iterdir())
    if len(roots) != 1 or not roots[0].is_dir():
        raise RuntimeError('Expected a single relocatable package root')
    return roots[0], actual


def audit_package(prefix, archive, kind, sources, configuration, platform,
                  require_clean=False, require_slcan=False, forbidden_paths=()):
    """Check package provenance, source licenses, dependencies and installed files."""
    packages = [('linkerbot-comm', 'comm')]
    if kind == 'hand':
        packages.append(('linkerbot-hand-cpp', 'hand'))
    for package, source_name in packages:
        source = sources[source_name]
        share = prefix / 'share' / package
        build_doc = 'docs/zh/windows-build.md' if source_name == 'comm' else 'docs/zh/guides/windows.md'
        documents = ['LICENSE', 'NOTICE', 'README.md', 'build-info.json', build_doc]
        if source_name == 'hand':
            documents += ['docs/zh/guides/usage.md', 'docs/en/guides/windows.md',
                          'docs/en/guides/usage.md']
        for relative in documents:
            if not (share / relative).is_file():
                raise RuntimeError(f'Required package entry missing: {package}/{relative}')
        for name in ['LICENSE', 'NOTICE', 'README.md', 'README.en.md', 'CHANGELOG.md', 'CONTRIBUTING.md']:
            original = Path(source['path']) / name
            if original.is_file() and (not (share / name).is_file()
                                       or (share / name).read_bytes() != original.read_bytes()):
                raise RuntimeError(f'Installed document differs from source: {package}/{name}')
        cmake = prefix / 'lib' / 'cmake' / package
        for suffix in ['-config.cmake', '-config-version.cmake']:
            if not (cmake / (package + suffix)).is_file():
                raise RuntimeError(f'CMake package entry missing: {package}{suffix}')
        metadata = json.loads((share / 'build-info.json').read_text(encoding='utf-8'))
        expected = {'version': source['version'], 'configuration': configuration,
                    'comm_sha': sources['comm']['sha']}
        if source_name == 'hand':
            expected['hand_sha'] = sources['hand']['sha']
        expected['system'] = 'Windows' if platform == 'win32' else 'Linux' if platform.startswith('linux') else 'Darwin'
        if any(metadata.get(key) != value for key, value in expected.items()):
            raise RuntimeError(f'Incorrect package version/provenance: {package}: {metadata}')
        if require_clean and metadata.get('source_dirty') is not False:
            raise RuntimeError(f'Release package contains dirty source metadata: {package}')
        if platform == 'win32':
            runtime = 'MDd' if configuration == 'Debug' else 'MD'
            if (metadata.get('runtime'), metadata.get('architecture'), metadata.get('toolset')) != (runtime, 'x64', '143'):
                raise RuntimeError(f'Incorrect Windows runtime/architecture/toolset: {package}')
            if not all(part in archive.name for part in ['Windows-x64-msvc143', configuration, '-' + runtime]):
                raise RuntimeError('Windows filename does not identify toolchain/configuration/runtime')
        library = 'linkerbot-comm' if source_name == 'comm' else 'linkerbot-cpp-sdk'
        library_name = library + '.lib' if platform == 'win32' else 'lib' + library + '.a'
        if not (prefix / 'lib' / library_name).is_file():
            raise RuntimeError(f'Library missing: {package}')

    files = sorted(p.relative_to(prefix).as_posix() for p in prefix.rglob('*') if p.is_file())
    for relative in files:
        lower = relative.lower()
        if any(part in lower for part in ['fixture', 'catch2', 'pcanbasic.dll', 'hcanbus.dll', 'libcanbus.so']):
            raise RuntimeError(f'Test/vendor component leaked into package: {relative}')
        if lower.endswith('.cmake'):
            contents = (prefix / relative).read_text(encoding='utf-8').replace('\\', '/')
            if any(Path(original).as_posix() in contents for original in forbidden_paths):
                raise RuntimeError(f'Source/build path leaked into CMake config: {relative}')
    required = ['include/linkerbot/can/pcan_transport.hpp']
    if require_slcan:
        required += ['include/linkerbot/can/slcan_transport.hpp', 'share/linkerbot-comm/docs/zh/slcan.md']
    if kind == 'hand':
        required.append('include/linkerbot/hand/adapter_transport.hpp')
    for relative in required:
        if not (prefix / relative).is_file():
            raise RuntimeError(f'Public package entry missing: {relative}')
    return files
