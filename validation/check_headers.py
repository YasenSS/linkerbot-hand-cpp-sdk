#!/usr/bin/env python3
"""Compile installed public headers as separate CMake translation units."""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from build_support import add_build_arguments, cmake_options

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--include', type=Path, required=True)
parser.add_argument('--compiler', help='Optional compiler override; omitted for Visual Studio')
parser.add_argument('--jobs', type=int, default=4)
parser.add_argument('--output', type=Path, required=True)
add_build_arguments(parser)
args = parser.parse_args()
include = args.include.resolve()
linux_only = {'linkerbot/can/socketcan_frame.hpp',
              'linkerbot/can/socketcan_transport.hpp',
              'linkerbot/compat/socketcan_transport.hpp'}
headers = sorted(p.relative_to(include).as_posix() for p in include.rglob('*.hpp'))
checked = [p for p in headers if sys.platform.startswith('linux') or p not in linux_only]
skipped = [p for p in headers if p not in checked]
logs = []
result = 1
if checked:
    with tempfile.TemporaryDirectory(prefix='linkerbot-headers-') as directory:
        source = Path(directory)
        for number, header in enumerate(checked):
            (source / f'header_{number}.cpp').write_text(f'#include <{header}>\n', encoding='utf-8')
        (source / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.16)\nproject(public-headers LANGUAGES CXX)\n'
            'file(GLOB units "${CMAKE_CURRENT_SOURCE_DIR}/header_*.cpp")\n'
            'add_library(public-headers OBJECT ${units})\n'
            'target_compile_features(public-headers PRIVATE cxx_std_17)\n'
            'target_include_directories(public-headers PRIVATE "${SDK_INCLUDE}" "${SDK_INCLUDE}/linkerbot/compat")\n'
            'if(MSVC)\n  target_compile_options(public-headers PRIVATE /utf-8)\nendif()\n',
            encoding='utf-8')
        commands = [['cmake', '-S', str(source), '-B', str(source / 'build'),
                     f'-DSDK_INCLUDE={include}', *cmake_options(args)],
                    ['cmake', '--build', str(source / 'build'), '--config', args.config,
                     '--parallel', str(args.jobs)]]
        if args.compiler:
            commands[0].append(f'-DCMAKE_CXX_COMPILER={args.compiler}')
        for command in commands:
            process = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            logs.append(process.stdout)
            result = process.returncode
            if result:
                print(process.stdout)
                break
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps({
    'configuration': args.config,
    'checked': checked,
    'skipped': [{'header': p, 'reason': 'Linux SocketCAN only'} for p in skipped],
    'exit': result, 'diagnostics': '\n'.join(logs),
}, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(f'{len(checked)} independent public headers; {len(skipped)} Linux-only headers skipped; exit={result}')
raise SystemExit(bool(result) or not checked)
