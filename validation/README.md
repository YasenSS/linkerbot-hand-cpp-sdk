# Validation tools

[English](README.md) | [简体中文](README.zh-CN.md)

- [Build and validation](../docs/en/guides/validation.md): offline targets, installed consumers and CI diagnosis.
- [Quality analysis](../docs/en/guides/quality.md): Report Card scope and completeness policy.
- [Asynchronous behavior vectors](README-async.md): optional C++/Python comparisons.
- [Hardware evidence](../docs/hardware-acceptance/README.en.md): historical reports and limits.

Run `python3 validation/check_docs.py` for documentation navigation and baseline checks.
Run `python3 -m unittest discover -s validation -p 'test_quality_tools.py'` for analysis helper tests.
Release checks are covered by `test_release_*.py`; they use temporary repositories and packages.
`check_headers.py` and `check_documented_examples.py` require an installed SDK prefix.
