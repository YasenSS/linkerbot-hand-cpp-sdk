#!/usr/bin/env python3
"""Validate paired builds, separate installs and relocated packages without hardware.

The same entry point runs on MSVC CI and Linux. It never executes motion examples.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from build_support import add_build_arguments, cmake_options, validation_options
from release_package import audit_package, project_version, unpack_archive

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--comm-source', type=Path, required=True)
parser.add_argument('--work-dir', type=Path, required=True)
parser.add_argument('--suite', choices=['all', 'no-drivers', 'native-loader'], default='all')
parser.add_argument('--quick', action='store_true',
                    help='Build and test only; skip installation, packaging and release acceptance')
parser.add_argument('--comm-sha', help='Require a clean comm checkout at this full SHA')
parser.add_argument('--hand-sha', help='Require a clean hand checkout at this full SHA')
parser.add_argument('--catch2-dir', type=Path)
parser.add_argument('--allow-downloads', action='store_true')
parser.add_argument('--require-slcan', action='store_true', help='Require stage-B comm source and package entries')
parser.add_argument('--jobs', type=int, default=4)
parser.add_argument('--package-format', choices=['ZIP', 'TGZ'], default='ZIP')
parser.add_argument('--expected-version', help='Require both source projects and packages at this version')
add_build_arguments(parser)
args = parser.parse_args()
hand = Path(__file__).resolve().parents[1]
comm = args.comm_source.resolve()
work = args.work_dir.resolve()
work.mkdir(parents=True, exist_ok=True)
evidence = work / 'evidence'
evidence.mkdir(exist_ok=True)
# Config/environment isolation is inherited by test and consumer processes.
os.environ['XDG_CONFIG_HOME'] = str(work / 'offline-config')
os.environ['PYTHONUTF8'] = '1'
os.environ.pop('LINKERBOT_CANFD_LIB', None)
os.environ.pop('LINKERBOT_PCAN_LIB', None)
report = {'configuration': args.config, 'suite': args.suite, 'platform': sys.platform,
          'quick': args.quick,
          'steps': [], 'sources': {}, 'status': 'running', 'require_slcan': args.require_slcan,
          'package_format': args.package_format}
relocations = []
if args.require_slcan and not (comm / 'include/linkerbot/can/slcan_transport.hpp').is_file():
    parser.error('--require-slcan needs a matching stage-B comm revision')


def save():
    (evidence / 'summary.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def run(name, command, cwd=None):
    print(f'[{args.config}/{args.suite}] {name}', flush=True)
    logfile = evidence / (name + '.log')
    with logfile.open('w', encoding='utf-8') as output:
        output.write(json.dumps([str(x) for x in command], ensure_ascii=False) + '\n')
        output.flush()
        process = subprocess.run([str(x) for x in command], cwd=cwd, stdout=output, stderr=subprocess.STDOUT)
    report['steps'].append({'step': name, 'exit': process.returncode, 'log': logfile.name})
    save()
    if process.returncode:
        print(logfile.read_text(encoding='utf-8', errors='replace')[-12000:])
        raise RuntimeError(f'{name} failed: {logfile}')
    return logfile


def source_info(name, source, expected):
    sha = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain', '--untracked-files=normal'], text=True)
    if expected and (len(expected) != 40 or sha != expected or dirty):
        raise RuntimeError(f'{name} must be a clean checkout at {expected}; actual {sha}, dirty={bool(dirty)}')
    version = project_version(source)
    if args.expected_version and version != args.expected_version:
        raise RuntimeError(f'{name} version must be {args.expected_version}; actual {version}')
    diff = subprocess.check_output(['git', '-C', str(source), 'diff', '--binary', 'HEAD'])
    (evidence / f'{name}-source.patch').write_bytes(diff)
    paths = subprocess.check_output(['git', '-C', str(source), 'ls-files', '-z', '--cached', '--others', '--exclude-standard'])
    files = {}
    for entry in paths.split(b'\0'):
        if entry:
            relative = entry.decode('utf-8')
            path = source / relative
            if path.is_file():
                files[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
    (evidence / f'{name}-source-files.json').write_text(json.dumps(files, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    report['sources'][name] = {'sha': sha, 'version': version, 'dirty': bool(dirty), 'status': dirty}


def configure(name, source, build, options):
    run(name, ['cmake', '-S', source, '-B', build, *cmake_options(args), *options])


def build(name, directory, targets=None):
    command = ['cmake', '--build', directory, '--config', args.config, '--parallel', args.jobs]
    if targets:
        command.extend(['--target', *targets])
    run(name, command)


def ctest(name, directory, required=(), minimum=1, native=None):
    command = ['ctest', '--test-dir', directory, '-C', args.config]
    if native is not None:
        command += ['-R' if native else '-E', 'native-loader|native-integration']
    listing = run(name + '-list', [*command, '--show-only=json-v1'])
    data = listing.read_text(encoding='utf-8')
    tests = json.loads(data[data.index('\n') + 1:])['tests']
    names = {test['name'] for test in tests}
    if len(tests) < minimum or not set(required).issubset(names):
        raise RuntimeError(f'{name}: missing tests; count={len(tests)}, expected={list(required)}')
    if args.require_slcan and name == 'comm-no-drivers' and sum(n.startswith('SLCAN') for n in names) < 10:
        raise RuntimeError('Stage-B SLCAN offline tests were not registered')
    report.setdefault('tests', {})[name] = {'count': len(tests), 'names': sorted(names)}
    run(name, [*command, '--output-on-failure', '--no-tests=error', '--timeout', '120',
               '--output-junit', evidence / (name + '.xml')])


def consumer(name, prefix, kind):
    package = 'linkerbot-comm' if kind == 'comm' else 'linkerbot-hand-cpp'
    source = prefix / 'share' / package / 'validation' / 'installed-consumer'
    directory = work / name
    configure(name + '-configure', source, directory,
              [f'-DCMAKE_PREFIX_PATH={prefix}', '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF',
               '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF'])
    build(name + '-build', directory)
    ctest(name + '-test', directory)


def package(name, directory, kind):
    output = work / 'packages' / name
    output.mkdir(parents=True, exist_ok=True)
    extension = '*.zip' if args.package_format == 'ZIP' else '*.tar.gz'
    if list(output.iterdir()):
        raise RuntimeError(f'{output} is not empty; use a fresh work directory')
    run(name + '-cpack', ['cpack', '--config', directory / 'CPackConfig.cmake', '-C', args.config,
                         '-G', args.package_format, '-B', output])
    archives = list(output.glob(extension))
    if len(archives) != 1:
        raise RuntimeError(f'{name}: expected exactly one {args.package_format} archive')
    archive = archives[0]
    # A package root already includes version/toolchain/configuration. Keep its
    # relocation outside a potentially deep workspace so MSVC relative includes
    # also fit MAX_PATH. Still exercise Unicode and spaces in the install prefix.
    relocation = tempfile.TemporaryDirectory(prefix='解压 SDK ')
    relocations.append(relocation)
    relocated = Path(relocation.name) / name
    prefix, actual = unpack_archive(archive, relocated)
    sources = {key: {**report['sources'][key], 'path': path}
               for key, path in [('hand', hand), ('comm', comm)]}
    files = audit_package(prefix, archive, kind, sources, args.config, sys.platform,
                          require_clean=bool(args.comm_sha or args.hand_sha),
                          require_slcan=args.require_slcan,
                          forbidden_paths=[hand, comm, work / 'comm-build', work / 'hand-build'])
    (evidence / (name + '-files.json')).write_text(json.dumps(files, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    report.setdefault('packages', []).append({'file': str(archive.relative_to(work)), 'sha256': actual,
                                             'bytes': archive.stat().st_size, 'entries': len(files)})
    # ZIP does not preserve executable permissions on Linux; TGZ already does.
    if args.package_format == 'ZIP':
        for executable in (prefix / 'bin').glob('*'):
            executable.chmod(executable.stat().st_mode | 0o111)
    consumer(name + '-relocated-consumer', prefix, kind)
    if kind == 'hand':
        executable = prefix / 'bin' / ('windows_readonly.exe' if sys.platform == 'win32' else 'windows_readonly')
        run(name + '-readonly-help', [executable, '--help'])
    return prefix


try:
    source_info('comm', comm, args.comm_sha)
    source_info('hand', hand, args.hand_sha)
    save()
    run('cmake-version', ['cmake', '--version'])
    common = [f'-DLINKERBOT_ALLOW_DEPENDENCY_DOWNLOADS={"ON" if args.allow_downloads else "OFF"}',
              '-DCMAKE_INSTALL_LIBDIR=lib']
    if args.catch2_dir:
        common.append(f'-DCatch2_DIR={args.catch2_dir.resolve()}')
    comm_build, hand_build = work / 'comm-build', work / 'hand-build'
    configure('comm-configure', comm, comm_build, [*common, '-DLINKERBOT_COMM_BUILD_TESTS=ON',
                                                 '-DLINKERBOT_COMM_BUILD_EXAMPLES=OFF'])
    build('comm-build', comm_build)
    configure('hand-configure', hand, hand_build, [*common, '-DLINKERBOT_COMM_PROVIDER=SOURCE', f'-DLINKERBOT_COMM_SOURCE_DIR={comm}',
               '-DLINKERBOT_COMM_BUILD_TESTS=OFF', '-DLINKERBOT_COMM_BUILD_EXAMPLES=OFF',
               '-DLINKERBOT_BUILD_TESTS=ON', '-DLINKERBOT_BUILD_EXAMPLES=ON', '-DLINKERBOT_BUILD_TOOLS=ON'])
    hand_targets = ['linkerbot-cpp-sdk', 'windows_readonly', 'l6_hop_readonly', 'tests', 'protocol-tests']
    if not args.quick:
        hand_targets.append('example')
    build('hand-build', hand_build, hand_targets)
    if args.suite in ['all', 'no-drivers']:
        ctest('comm-no-drivers', comm_build, minimum=30, native=False)
        ctest('hand-no-drivers', hand_build,
              required=['comm', 'discovery', 'protocol-vectors', 'async-behavior-vectors', 'release-package-audit',
                        'windows_readonly_help', 'windows_readonly_invalid',
                        'windows_readonly_slcan_fd', 'windows_readonly_slcan_allowlist',
                        'windows_readonly_slcan_uart', 'windows_readonly_slcan_fd_rate',
                        'windows_readonly_slcan_profile', 'windows_readonly_slcan_startup',
                        'l6_hop_readonly_help', 'l6_hop_readonly_invalid'], native=False)
    if args.suite in ['all', 'native-loader']:
        ctest('comm-native-loader', comm_build, required=['pcan-native-loader', 'vendor-native-loader'], native=True)
        ctest('hand-native-loader', hand_build, required=['adapter-native-integration'], native=True)
    if not args.quick and args.suite in ['all', 'no-drivers']:
        # A comm-only installation is consumed before any hand package exists.
        comm_prefix = work / 'comm-only-install'
        run('comm-install', ['cmake', '--install', comm_build, '--config', args.config, '--prefix', comm_prefix])
        consumer('comm-only-consumer', comm_prefix, 'comm')
        # PREBUILT prevents silently using the sibling source checkout.
        installed_build = work / 'hand-with-installed-comm'
        configure('installed-comm-hand-configure', hand, installed_build, [
            *common, '-DLINKERBOT_COMM_PROVIDER=PREBUILT', f'-DLINKERBOT_COMM_PREBUILT_PREFIX={comm_prefix}',
            '-DLINKERBOT_BUILD_TESTS=OFF', '-DLINKERBOT_BUILD_EXAMPLES=ON', '-DLINKERBOT_BUILD_TOOLS=OFF'])
        build('installed-comm-hand-build', installed_build, ['linkerbot-cpp-sdk', 'windows_readonly'])
        separate_prefix = work / 'separate-hand-install'
        run('separate-hand-install', ['cmake', '--install', installed_build, '--config', args.config, '--prefix', separate_prefix])
        configure('separate-prefix-consumer-configure', hand / 'validation/installed-consumer',
                  work / 'separate-prefix-consumer', [f'-DCMAKE_PREFIX_PATH={separate_prefix};{comm_prefix}'])
        build('separate-prefix-consumer-build', work / 'separate-prefix-consumer')
        ctest('separate-prefix-consumer-test', work / 'separate-prefix-consumer')
        # Remove both original build trees from the consumer's package search.
        package('comm', comm_build, 'comm')
        paired_prefix = package('hand', hand_build, 'hand')
        run('public-headers', [sys.executable, hand / 'validation/check_headers.py', '--include',
            paired_prefix / 'include', '--output', evidence / 'public-headers.json', '--jobs', args.jobs,
            *validation_options(args)])
        run('documented-examples', [sys.executable, hand / 'validation/check_documented_examples.py',
            '--prefix', paired_prefix, '--build-dir', work / 'documented-examples', *validation_options(args)])
        generator = [f'-DVALIDATION_{name.upper()}={getattr(args, name)}'
                     for name in ['generator', 'architecture', 'toolset'] if getattr(args, name)]
        # CMake try_compile adds several nested directories below its cwd.
        compatibility = tempfile.TemporaryDirectory(prefix='lb-compat-')
        relocations.append(compatibility)
        run('package-compatibility', ['cmake', f'-DHAND_PREFIX={paired_prefix}',
            f'-DHAND_VERSION={report["sources"]["hand"]["version"]}', *generator,
            '-P', hand / 'validation/check_package_compatibility.cmake'], cwd=compatibility.name)
    if report.get('packages'):
        manifest = {'version': report['sources']['hand']['version'], 'configuration': args.config,
                    'platform': sys.platform, 'format': args.package_format,
                    'sources': report['sources'],
                    'packages': [{**item, 'file': Path(item['file']).relative_to('packages').as_posix()}
                                 for item in report['packages']]}
        (work / 'packages' / 'release-manifest.json').write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    report['status'] = 'passed'
except Exception as error:
    report['status'] = 'failed'
    report['error'] = str(error)
    print(str(error), file=sys.stderr)
finally:
    for relocation in relocations:
        relocation.cleanup()
    for directory in [work / 'comm-build', work / 'hand-build']:
        for relative in ['CMakeCache.txt', 'Testing/Temporary/LastTest.log']:
            source = directory / relative
            if source.exists():
                shutil.copy2(source, evidence / (directory.name + '-' + source.name))
    save()
print(f"Validation {report['status']}: {evidence / 'summary.json'}", flush=True)
raise SystemExit(report['status'] != 'passed')
