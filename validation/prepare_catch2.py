"""Build one pinned Catch2 installation for both transport and hand CI tests."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace

from build_support import add_build_arguments, cmake_options


VERSION = '3.8.1'
REVISION = '2b60af89e23d28eefc081bc930831ee9d45ea58b'


def prepare(prefix, work, configuration, jobs, generator=None, compiler_cache=None):
    prefix, work = Path(prefix).resolve(), Path(work).resolve()
    options = cmake_options(SimpleNamespace(config=configuration, generator=generator,
                                           architecture=None, toolset=None, compiler_cache=compiler_cache))
    identity = {
        'revision': REVISION, 'configuration': configuration,
        'cxx_standard': 17,
        'platform': sys.platform, 'generator': generator or os.environ.get('CMAKE_GENERATOR', ''),
        'compiler': os.environ.get('VCToolsVersion', ''),
        'sdk': os.environ.get('WindowsSDKVersion', ''),
        'architecture': os.environ.get('VSCMD_ARG_TGT_ARCH', ''),
        'compiler_cache': compiler_cache,
    }
    marker = prefix / 'ci-identity.json'
    config = prefix / 'lib/cmake/Catch2/Catch2Config.cmake'
    if marker.is_file() and json.loads(marker.read_text(encoding='utf-8')) == identity and config.is_file():
        print(f'Reusing Catch2 {VERSION} {configuration}: {prefix}', flush=True)
        return config.parent
    if prefix.exists() and any(prefix.iterdir()):
        raise RuntimeError('Catch2 prefix is incomplete or incompatible; use a fresh cache key/prefix')
    source, build = work / 'source', work / 'build'
    work.mkdir(parents=True, exist_ok=True)
    subprocess.run(['git', 'clone', '--depth', '1', '--branch', 'v' + VERSION,
                    'https://github.com/catchorg/Catch2.git', str(source)], check=True)
    actual = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != REVISION:
        raise RuntimeError('Catch2 tag does not match the pinned source revision')
    subprocess.run(['cmake', '-S', str(source), '-B', str(build),
                    *options, f'-DCMAKE_INSTALL_PREFIX={prefix}',
                    '-DCMAKE_INSTALL_LIBDIR=lib', '-DBUILD_SHARED_LIBS=OFF', '-DCATCH_BUILD_TESTING=OFF',
                    '-DCMAKE_CXX_STANDARD=17', '-DCMAKE_CXX_STANDARD_REQUIRED=ON', '-DCMAKE_CXX_EXTENSIONS=OFF',
                    '-DCATCH_INSTALL_DOCS=OFF', '-DCATCH_INSTALL_EXTRAS=ON',
                    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL'], check=True)
    subprocess.run(['cmake', '--build', str(build), '--config', configuration,
                    '--parallel', str(jobs)], check=True)
    subprocess.run(['cmake', '--install', str(build), '--config', configuration], check=True)
    if not config.is_file():
        raise RuntimeError('Catch2 installation is missing its CMake package')
    marker.write_text(json.dumps(identity, sort_keys=True) + '\n', encoding='utf-8')
    return config.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    add_build_arguments(parser)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.architecture or args.toolset:
        parser.error('Use a native compiler environment rather than generator architecture/toolset overrides')
    prepare(args.prefix, args.work_dir, args.config, args.jobs, args.generator, args.compiler_cache)


if __name__ == '__main__':
    main()
