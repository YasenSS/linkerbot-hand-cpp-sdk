# C++ SDK Documentation

[简体中文](README.md) | [English](README.en.md)

- [English documentation](en/README.md): interface setup, connections, model tutorials, general guides, and references.
- [Hardware acceptance and reports](hardware-acceptance/README.md) (Chinese): shared historical acceptance records.

## Organization

`zh` and `en` use the same directory structure and filenames; only the language directory differs.

| Directory | Contents |
| --- | --- |
| `connections/` | Interface preparation, automatic/manual connections, adapters, and troubleshooting |
| `tutorials/` | Step-by-step model examples and support status |
| `guides/` | Build/install, general APIs, asynchronous commands, polling, and lifecycle |
| `reference/` | Capability tables, numeric/firmware limits, discovery, and compatibility |
| `hardware-acceptance/` | Historical hardware reports organized by model and shared by both languages |

Add model tutorials to `tutorials`, common usage to `guides`, and connection steps to `connections`.
Maintain numeric ranges and firmware limits in `reference` and link to them from other pages.
Acceptance claims apply only to the date, revision, and hardware in each report.
Keep corresponding language pages, language indexes, and affected links in sync.
