"""Measure Report Card tools without mixing timing output into diagnostics."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import time
import uuid


TOOLS = ('clang-format', 'cppcheck', 'clang-tidy')


def child_setup():
    # The action kills a timed-out wrapper. Kill its analyzer too, including
    # the race where the wrapper exited just before prctl was installed.
    parent = os.getppid()
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(1, signal.SIGKILL, 0, 0, 0) != 0:  # PR_SET_PDEATHSIG
        raise OSError(ctypes.get_errno(), 'prctl(PR_SET_PDEATHSIG) failed')
    if os.getppid() != parent or parent == 1:
        os.kill(os.getpid(), signal.SIGKILL)


def write_record(path, record):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(record, ensure_ascii=False) + '\n', encoding='utf-8')
    temporary.replace(path)


def run_timed(tool, command):
    directory = os.environ.get('REPORT_TIMING_DIR')
    if not directory:
        return subprocess.run(command, check=False).returncode
    records = Path(directory) / 'records'
    records.mkdir(parents=True, exist_ok=True)
    path = records / (uuid.uuid4().hex + '.json')
    arguments = command[1:]
    phase = 'version' if '--version' in arguments else 'analysis'
    source = arguments[-1] if phase == 'analysis' and tool != 'cppcheck' else None
    if source:
        source = os.path.relpath(source, Path.cwd())
    record = {'tool': tool, 'phase': phase, 'file': source, 'command': command,
              'started_at': time.time(), 'start': time.monotonic(), 'end': None, 'exit': None}
    write_record(path, record)
    try:
        process = subprocess.run(command, check=False,
                                 preexec_fn=child_setup if sys.platform == 'linux' else None)
        record['exit'] = process.returncode
        return process.returncode
    finally:
        record['end'] = time.monotonic()
        write_record(path, record)


def prepare(config, directory):
    directory = directory.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    data = json.loads(config.read_text(encoding='utf-8'))
    # Cppcheck already has an adapter; time the native invocation inside it.
    for tool in ('clang-format', 'clang-tidy'):
        wrapper = directory / tool
        command = [sys.executable, str(Path(__file__).resolve()), 'run', tool, data['tools'][tool]]
        wrapper.write_text('#!/bin/sh\nexec ' + shlex.join(command) + ' "$@"\n', encoding='utf-8')
        wrapper.chmod(0o755)
        data['tools'][tool] = str(wrapper)
    (directory / 'config.json').write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')


def summarize(records, report=None):
    analysis = [record for record in records if record['phase'] == 'analysis']
    tools = []
    for tool in TOOLS:
        rows = [r for r in analysis if r['tool'] == tool]
        finished = [r for r in rows if r['end'] is not None]
        tools.append({'tool': tool, 'calls': len(rows), 'finished': len(finished),
                      'failures': sum(r['exit'] != 0 for r in finished),
                      'wall_seconds': round(max(r['end'] for r in finished) - min(r['start'] for r in rows), 3)
                      if finished else None,
                      'summed_seconds': round(sum(r['end'] - r['start'] for r in finished), 3)})
    coverage_errors = []
    if report is not None:
        checks = {check['tool']: check for check in report.get('checks', [])}
        for row in tools:
            check = checks.get(row['tool'], {})
            expected = 1 if row['tool'] == 'cppcheck' else check.get('total')
            if not check or row['calls'] != expected or row['finished'] != row['calls']:
                coverage_errors.append(row['tool'] + ': timing does not cover the reported analysis')
    slowest = sorted((dict(file=r['file'], seconds=round(r['end'] - r['start'], 3),
                           exit=r['exit'], command=r['command'])
                      for r in analysis if r['tool'] == 'clang-tidy' and r['end'] is not None),
                     key=lambda row: row['seconds'], reverse=True)[:20]
    return {'tools': tools, 'slowest_clang_tidy': slowest, 'coverage_errors': coverage_errors,
            'report_available': report is not None,
            'unfinished_calls': sum(r['end'] is None for r in analysis),
            'version_calls': sum(r['phase'] == 'version' for r in records)}


def markdown(summary):
    lines = ['## Report Card analyzer timings', '',
             'Wall span is elapsed time from the first recorded call to the last completion of each tool.',
             'Summed call time includes overlapping work; it is not CPU time or job duration.',
             'Version probes are excluded. Cppcheck is timed as one complete database invocation.', '',
             '| Tool | Calls / finished | Failures | Wall span (s) | Summed call time (s) |',
             '| --- | ---: | ---: | ---: | ---: |']
    for row in summary['tools']:
        wall = '-' if row['wall_seconds'] is None else f"{row['wall_seconds']:.3f}"
        lines.append(f"| {row['tool']} | {row['calls']} / {row['finished']} | {row['failures']} | "
                     f"{wall} | {row['summed_seconds']:.3f} |")
    lines += ['', '### Slowest clang-tidy invocations', '',
              'Each invocation is retained separately, including different configurations of the same file.', '',
              '| File | Seconds | Exit |', '| --- | ---: | ---: |']
    for row in summary['slowest_clang_tidy']:
        name = row['file'].replace('|', '&#124;').replace('\n', ' ')
        lines.append(f"| {name} | {row['seconds']:.3f} | {row['exit']} |")
    if summary['unfinished_calls']:
        lines += ['', f"Unfinished invocations: {summary['unfinished_calls']}. Timings are partial."]
    if not summary['report_available']:
        lines += ['', 'No report is available; timing coverage has not been verified.']
    if summary['coverage_errors']:
        lines += ['', *summary['coverage_errors']]
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='operation', required=True)
    setup = commands.add_parser('prepare')
    setup.add_argument('--config', type=Path, required=True)
    setup.add_argument('--directory', type=Path, required=True)
    run = commands.add_parser('run')
    run.add_argument('tool', choices=TOOLS)
    run.add_argument('command', nargs=argparse.REMAINDER)
    summary = commands.add_parser('summarize')
    summary.add_argument('--directory', type=Path, required=True)
    summary.add_argument('--output', type=Path, required=True)
    summary.add_argument('--report', type=Path)
    args = parser.parse_args()
    if args.operation == 'prepare':
        prepare(args.config, args.directory)
    elif args.operation == 'run':
        if not args.command:
            parser.error('run requires an analyzer command')
        code = run_timed(args.tool, args.command)
        if code < 0:
            # Preserve signal termination as well as ordinary exit codes.
            if -code not in (signal.SIGKILL, signal.SIGSTOP):
                signal.signal(-code, signal.SIG_DFL)
            os.kill(os.getpid(), -code)
        return code
    else:
        rows = [json.loads(path.read_text(encoding='utf-8'))
                for path in sorted((args.directory / 'records').glob('*.json'))]
        report = json.loads(args.report.read_text(encoding='utf-8')) if args.report and args.report.is_file() else None
        result = summarize(rows, report)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        content = markdown(result)
        args.output.with_suffix('.md').write_text(content, encoding='utf-8')
        print(content, end='')
        if os.environ.get('GITHUB_STEP_SUMMARY'):
            with Path(os.environ['GITHUB_STEP_SUMMARY']).open('a', encoding='utf-8') as output:
                output.write(content)
        return int(bool(result['coverage_errors']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
