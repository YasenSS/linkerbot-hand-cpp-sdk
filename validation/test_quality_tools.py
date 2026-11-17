import copy
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from check_report_complete import validate
from prepare_report_db import prepare
from cppcheck_report import command_database
from report_timing import prepare as prepare_timing, summarize, markdown


class QualityToolsTests(unittest.TestCase):
    def test_aplus_gate_rejects_downgrades_and_retained_findings(self):
        report = {"mode": "full", "analysis_complete": True, "grade": "A+",
                  "score": 100, "status": "passed", "checks": [
            {"tool": tool, "status": "passed", "analyzed": 2, "total": 2, "diagnostics": []}
            for tool in ("clang-format", "cppcheck", "clang-tidy")]}
        self.assertEqual(validate(report, require_aplus=True), 0)
        for key, value in (("grade", "A"), ("score", 99.99), ("status", "findings"),
                           ("analysis_complete", False)):
            invalid = copy.deepcopy(report)
            invalid[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate(invalid, require_aplus=True)
        for diagnostic in ({"severity": "warning"}, {"severity": "information"}):
            invalid = copy.deepcopy(report)
            invalid["checks"][1]["diagnostics"].append(diagnostic)
            with self.assertRaises(ValueError):
                validate(invalid, require_aplus=True)

    def test_shared_implementation_headers_remain_in_scope(self):
        config = (Path(__file__).resolve().parents[1] / ".clang-tidy").read_text()
        value = next(line.split(":", 1)[1].strip() for line in config.splitlines()
                     if line.startswith("HeaderFilterRegex:"))
        pattern = json.loads(value)
        for path in (
            "/repo/tests/protocol/l6/completion_vector_cases.hpp",
            "/repo/tests/hand/l6/../../protocol/l6/completion_vector_cases.hpp",
            "/repo/tools/restore_state.hpp",
            "/repo/tests/hand/contracts/../../../tools/restore_state.hpp",
        ):
            with self.subTest(path=path):
                self.assertRegex(path, pattern)

    def test_complete_findings_are_reported_not_hidden(self):
        report = {"mode": "full", "analysis_complete": True, "grade": "C", "checks": [
            {"tool": tool, "status": "findings", "analyzed": 2, "total": 2,
             "diagnostics": [{"message": "retained finding"}]}
            for tool in ("clang-format", "cppcheck", "clang-tidy")]}
        self.assertEqual(validate(report), 3)
        for invalid in (False, None):
            report["analysis_complete"] = invalid
            with self.assertRaises(ValueError):
                validate(report)
        report["analysis_complete"] = True
        report["checks"][0]["analyzed"] = 1
        with self.assertRaises(ValueError):
            validate(report)
        report["checks"][0]["analyzed"] = 2
        report["checks"][0]["status"] = "error"
        with self.assertRaises(ValueError):
            validate(report)

    def test_cppcheck_preserves_cmake_string_macro_escaping(self):
        command = r'c++ -DTEST_DIR=\"/tmp/test-config\" -c main.cpp'
        entry = {"directory": "/tmp", "file": "main.cpp", "command": command,
                 "arguments": ["c++", '-DTEST_DIR="/tmp/test-config"', "-c", "main.cpp"]}
        result = command_database([entry])
        self.assertEqual(result[0]["command"], command)
        self.assertNotIn("arguments", result[0])
        self.assertIn("arguments", entry)
        with self.assertRaises(ValueError):
            command_database([{ "directory": "/tmp", "file": "main.cpp", "arguments": ["c++"] }])

    def test_database_keeps_sdk_units_and_excludes_dependencies(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            db = root / "db.json"
            entries = [{"directory": str(root), "file": file,
                        "arguments": ["c++", "-isystem", "headers", "-c", file]}
                       for file in ("src/hand.cpp", "tests/case.cpp", "external/lib.cpp", "build/generated.cpp")]
            db.write_text(json.dumps(entries))
            result = prepare(root, [db])
            self.assertEqual([entry["file"] for entry in result], ["src/hand.cpp", "tests/case.cpp"])
            self.assertIn("-I", result[0]["arguments"])
            db.write_text("[]")
            with self.assertRaises(ValueError):
                prepare(root, [db])

class ReportTimingTests(unittest.TestCase):
    script = Path(__file__).with_name('report_timing.py').resolve()

    def invoke(self, directory, *command):
        return subprocess.run([sys.executable, str(self.script), 'run', 'clang-tidy', *command],
                              env={**os.environ, 'REPORT_TIMING_DIR': str(directory)},
                              capture_output=True, timeout=10)

    def records(self, directory):
        return [json.loads(path.read_text()) for path in (Path(directory) / 'records').glob('*.json')]

    def test_binary_output_arguments_and_nonzero_exit_are_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            code = 'import os,sys; os.write(1,b"out\\xff"); os.write(2,b"err\\xfe"); sys.exit(7)'
            result = self.invoke(tmp, sys.executable, '-c', code, 'space and 中文.cpp')
            self.assertEqual((result.stdout, result.stderr, result.returncode), (b'out\xff', b'err\xfe', 7))
            row, = self.records(tmp)
            self.assertEqual(row['command'], [sys.executable, '-c', code, 'space and 中文.cpp'])
            self.assertEqual(row['file'], 'space and 中文.cpp')
            self.assertEqual(row['exit'], 7)
            self.assertGreater(row['end'], row['start'])

    def test_generated_wrapper_only_changes_tool_paths(self):
        with tempfile.TemporaryDirectory(prefix='timing space ') as tmp:
            root = Path(tmp)
            config = root / 'original.json'
            original = {'tools': {'clang-format': sys.executable, 'clang-tidy': sys.executable,
                                  'cppcheck': 'validation/cppcheck-report.sh'},
                        'jobs': 4, 'timeout_seconds': 300, 'exclude': ['build']}
            config.write_text(json.dumps(original))
            prepare_timing(config, root / 'wrappers with spaces')
            prepared = json.loads((root / 'wrappers with spaces/config.json').read_text())
            for tool in ('clang-format', 'clang-tidy'):
                result = subprocess.run([prepared['tools'][tool], '-c',
                                         'import sys; print(repr(sys.argv[1:]))', 'a b', '中文'],
                                        env={**os.environ, 'REPORT_TIMING_DIR': tmp},
                                        capture_output=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.decode().strip(), repr(['a b', '中文']))
                prepared['tools'][tool] = original['tools'][tool]
            self.assertEqual(prepared, original)
            self.assertEqual(json.loads(config.read_text()), original)

    def test_parallel_calls_do_not_overwrite_records_and_versions_are_excluded(self):
        with tempfile.TemporaryDirectory() as tmp:
            with ThreadPoolExecutor(max_workers=4) as pool:
                results = list(pool.map(lambda i: self.invoke(tmp, sys.executable, '-c', 'pass', str(i)), range(8)))
            self.assertTrue(all(result.returncode == 0 for result in results))
            self.assertEqual(self.invoke(tmp, sys.executable, '--version').returncode, 0)
            rows = self.records(tmp)
            self.assertEqual(len(rows), 9)
            summary = summarize(rows)
            self.assertEqual(summary['version_calls'], 1)
            self.assertEqual(next(r for r in summary['tools'] if r['tool'] == 'clang-tidy')['finished'], 8)

    def test_summary_handles_overlap_duplicate_files_failure_and_partial_records(self):
        def row(tool, start, end, code=0):
            return dict(tool=tool, phase='analysis', file='same.cpp', start=start, end=end,
                        exit=code, command=[tool, str(start), 'same.cpp'])
        rows = [row('clang-tidy', 0, 5), row('clang-tidy', 1, 4, 2),
                row('clang-format', 0, 1), row('cppcheck', 1, 2)]
        report = {'checks': [{'tool': tool, 'total': 2 if tool == 'clang-tidy' else 1}
                             for tool in ('clang-format', 'cppcheck', 'clang-tidy')]}
        result = summarize(rows, report)
        self.assertEqual(result['coverage_errors'], [])
        tidy = result['tools'][2]
        self.assertEqual((tidy['wall_seconds'], tidy['summed_seconds'], tidy['failures']), (5, 8, 1))
        self.assertEqual([r['seconds'] for r in result['slowest_clang_tidy']], [5, 3])
        rows.append(row('clang-tidy', 6, None, None))
        result = summarize(rows, report)
        self.assertEqual(result['unfinished_calls'], 1)
        self.assertTrue(result['coverage_errors'])
        self.assertIn('Timings are partial', markdown(result))
        self.assertIn('coverage has not been verified', markdown(summarize([])))

    @unittest.skipUnless(os.name == 'posix', 'requires POSIX signal exit status')
    def test_child_signal_exit_is_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = self.invoke(tmp, sys.executable, '-c', 'import os,signal; os.kill(os.getpid(), signal.SIGKILL)')
            self.assertEqual(result.returncode, -signal.SIGKILL)
            self.assertEqual(self.records(tmp)[0]['exit'], -signal.SIGKILL)

    @unittest.skipUnless(sys.platform == 'linux', 'CI timeout cleanup uses Linux parent-death signals')
    def test_killed_wrapper_does_not_leave_analyzer_running(self):
        with tempfile.TemporaryDirectory() as tmp:
            code = 'import os,sys; print(os.getpid(), flush=True); sys.stdin.buffer.read()'
            process = subprocess.Popen([sys.executable, str(self.script), 'run', 'clang-tidy',
                                        sys.executable, '-c', code], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       env={**os.environ, 'REPORT_TIMING_DIR': tmp})
            child = None
            try:
                with selectors.DefaultSelector() as ready:
                    ready.register(process.stdout, selectors.EVENT_READ)
                    self.assertTrue(ready.select(timeout=5), 'analyzer did not start')
                child = int(process.stdout.readline())
                process.kill()
                process.wait(timeout=5)
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    status = Path(f'/proc/{child}/stat')
                    if not status.exists() or status.read_text().split()[2] == 'Z':
                        break
                    time.sleep(0.02)
                else:
                    self.fail('analyzer survived the wrapper timeout')
                self.assertIsNone(self.records(tmp)[0]['end'])
            finally:
                if child is not None:
                    try:
                        os.kill(child, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=5)


class DocumentationAnchorTests(unittest.TestCase):
    def test_unicode_duplicate_and_explicit_anchors(self):
        from check_docs import anchors
        result = anchors('# 中文标题\n## API `value`\n## API `value`\n<a id="stable"></a>\n```cpp\n# ignored\n```')
        self.assertEqual(result, {'中文标题', 'api-value', 'api-value-1', 'stable'})


if __name__ == "__main__":
    unittest.main()
