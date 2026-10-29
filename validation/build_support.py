"""CMake configuration shared by offline validation entry points."""


def add_build_arguments(parser):
    parser.add_argument('--config', choices=['Debug', 'Release'], default='Release')
    parser.add_argument('--generator')
    parser.add_argument('--architecture')
    parser.add_argument('--toolset')


def cmake_options(args):
    options = [f'-DCMAKE_BUILD_TYPE={args.config}']
    for flag, name in [('-G', 'generator'), ('-A', 'architecture'), ('-T', 'toolset')]:
        value = getattr(args, name, None)
        if value:
            options.extend([flag, value])
    return options


def validation_options(args):
    options = ['--config', args.config]
    for name in ['generator', 'architecture', 'toolset']:
        value = getattr(args, name, None)
        if value:
            options.extend(['--' + name, value])
    return options
