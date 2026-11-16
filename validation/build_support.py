"""CMake configuration shared by offline validation entry points."""
import os


def add_build_arguments(parser):
    parser.add_argument('--config', choices=['Debug', 'Release'], default='Release')
    parser.add_argument('--generator')
    parser.add_argument('--architecture')
    parser.add_argument('--toolset')
    parser.add_argument('--compiler-cache', choices=['sccache', 'ccache'],
                        help='Optional compiler launcher for Ninja/Makefile builds')


def cmake_options(args):
    options = [f'-DCMAKE_BUILD_TYPE={args.config}']
    for flag, name in [('-G', 'generator'), ('-A', 'architecture'), ('-T', 'toolset')]:
        value = getattr(args, name, None)
        if value:
            options.extend([flag, value])
    if args.compiler_cache:
        generator = args.generator or os.environ.get('CMAKE_GENERATOR', '')
        if not (generator.startswith('Ninja') or 'Makefiles' in generator):
            raise ValueError('Compiler caching requires an explicit Ninja or Makefile generator')
        options.extend([f'-DCMAKE_C_COMPILER_LAUNCHER={args.compiler_cache}',
                        f'-DCMAKE_CXX_COMPILER_LAUNCHER={args.compiler_cache}',
                        '-DCMAKE_POLICY_DEFAULT_CMP0141=NEW',
                        '-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>'])
    return options


def validation_options(args):
    options = ['--config', args.config]
    for name in ['generator', 'architecture', 'toolset', 'compiler_cache']:
        value = getattr(args, name, None)
        if value:
            options.extend(['--' + name.replace('_', '-'), value])
    return options
