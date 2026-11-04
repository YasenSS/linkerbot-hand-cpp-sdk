"""Reject incomplete static analysis without presenting findings as a clean grade."""
import argparse
import json
from pathlib import Path


def validate(report):
    if report.get("mode") != "full" or report.get("analysis_complete") is not True:
        raise ValueError("A complete full-mode report is required")
    checks = report.get("checks", [])
    expected = {"clang-format", "cppcheck", "clang-tidy"}
    if len(checks) != 3 or {check.get("tool") for check in checks} != expected:
        raise ValueError("Report must include each of the three analyzers exactly once")
    for check in checks:
        analyzed, total = check.get("analyzed"), check.get("total")
        if type(analyzed) is not int or type(total) is not int or total <= 0 or analyzed != total:
            raise ValueError(f"{check.get('tool')}: incomplete unit coverage")
        if check.get("status") not in {"passed", "findings"}:
            raise ValueError(f"{check.get('tool')}: tool failed or was skipped")
    return sum(len(check.get("diagnostics", [])) for check in checks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    report = json.loads(args.report.read_text(encoding="utf-8"))
    count = validate(report)
    print(f"Complete analysis: grade={report.get('grade')}, findings={count}; not a zero-findings gate")


if __name__ == "__main__":
    main()
