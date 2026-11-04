# C++ Report Card snapshot

[English](README.md) | [简体中文](README.zh-CN.md)

[![C++ Report Card](badge.svg)](https://github.com/soulteary/linkerbot-hand-cpp-sdk/actions/runs/41736844903)

This SVG is the unmodified artifact from the successful
[full ARM64 report](https://github.com/soulteary/linkerbot-hand-cpp-sdk/actions/runs/41736844903)
for main commit `12cb694a3c5534f41372fd40a67d11bb03e33471`, using C++ Report Card Action
v1.0.2 at `9458d052f286c576d557c34d8ad43096cedbf561`.

- Analysis complete: `true`; grade: **B**; score: **84.68/100**.
- clang-format: **581/581 files**, 0 diagnostics.
- Cppcheck: **342/342 compilation entries**, 19 diagnostics.
- clang-tidy: **342/342 compilation entries**, 202 diagnostics.

The score is a source-analysis baseline, not test coverage or hardware validation.
Diagnostics require review; they are not all confirmed defects. The current gate
requires complete analysis and zero error-severity diagnostics, and preserves warnings, so a successful workflow does
not imply a zero-diagnostic report.

The README score badge is this dated snapshot; the adjacent CI badge shows the
latest main-branch workflow status. Download the run's
`cppreportcard-ubuntu-24.04-arm` artifact for the original JSON, Markdown, SVG and
compilation database. New runs upload artifacts without committing them automatically.
Refresh the SVG, source commit, run link and counts together from a completed run.
See the [quality guide](../../docs/en/guides/quality.md) for scope and policy.

The reviewed baseline improved from F (59/100, 717 diagnostics) to B (84.68/100,
221 diagnostics). The report contains no error-severity diagnostics. See the
[diagnostic review](../../docs/en/guides/quality-review.md) for fixes and remaining work.
