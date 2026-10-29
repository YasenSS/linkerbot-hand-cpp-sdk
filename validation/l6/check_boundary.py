#!/usr/bin/env python3
"""Enforce the L6 protocol and production execution boundaries, independently of other models."""
import argparse
import pathlib
import re
import shlex
import subprocess

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--comm-include', required=True, type=pathlib.Path)
parser.add_argument('--compiler', default='c++')
args = parser.parse_args()
source = pathlib.Path(__file__).resolve().parents[2]
output = subprocess.check_output([
    args.compiler, '-std=c++17', '-MM', '-x', 'c++',
    '-I', str(source / 'include'), '-I', str(args.comm_include), '-'],
    input='#include <linkerbot/hand/l6/protocol_backend.hpp>\n', text=True)
for dependency in shlex.split(output.replace('\\\n', ' '))[1:]:
    path = pathlib.Path(dependency)
    if not path.is_file():
        continue
    if '/transport/' in dependency or '/hand/runtime/' in dependency or (
        '/can/' in dependency and not dependency.endswith(('/message.hpp', '/frame.hpp', '/errors.hpp'))):
        raise SystemExit(f'protocol execution dependency: {dependency}')
    code = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(), flags=re.S)
    if re.search(r'#\s*include\s*[<"](?:thread|mutex|condition_variable|future|shared_mutex)[>"]|'
                 r'\b(?:steady_clock|system_clock|Clock)::now\s*\(|\b(?:wait_for|wait_until|sleep_for|sleep_until)\s*\(', code):
        raise SystemExit(f'protocol execution primitive: {dependency}')

paths = list((source / 'src/hand/l6').glob('*.cpp')) + list((source / 'include/linkerbot/hand/l6').rglob('*.hpp'))
for path in paths:
    code = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(), flags=re.S)
    if re.search(r'\b(?:ResponseWaiter|RequestRelay|DataRelay)\s*[<{]|'
                 r'\bstd::(?:thread|condition_variable)\b|'
                 r'\b(?:dispatcher_|dispatcher)\s*(?:->|\.)\s*(?:send|send_batch_until|subscribe)\s*\(', code):
        raise SystemExit(f'L6 owns an execution core outside DeviceRuntime: {path}')
print('L6 protocol transitive dependencies and production execution boundary: PASS')
