import json
from pathlib import Path
import tempfile
import unittest
from check_report_complete import validate
from prepare_report_db import prepare
from cppcheck_report import command_database


class QualityToolsTests(unittest.TestCase):
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

class DocumentationAnchorTests(unittest.TestCase):
    def test_unicode_duplicate_and_explicit_anchors(self):
        from check_docs import anchors
        result = anchors('# 中文标题\n## API `value`\n## API `value`\n<a id="stable"></a>\n```cpp\n# ignored\n```')
        self.assertEqual(result, {'中文标题', 'api-value', 'api-value-1', 'stable'})


if __name__ == "__main__":
    unittest.main()
