"""Merge build databases, retaining only translation units owned by this SDK."""
import argparse
import json
from pathlib import Path
import shlex
import re


def prepare(root, databases):
    root = Path(root).resolve()
    entries = []
    for database in databases:
        for original in json.loads(Path(database).read_text(encoding="utf-8")):
            entry = dict(original)
            directory = Path(entry["directory"]).resolve()
            source = (directory / entry["file"]).resolve()
            if not source.is_relative_to(root):
                continue
            if source.relative_to(root).parts[0] in {"build", "external"}:
                continue
            command = entry.get("arguments") or shlex.split(entry["command"])
            # Keep CMake's shell escaping for Cppcheck 2.13: it strips literal
            # quotes from -D string values in an arguments-only database.
            if "command" in entry:
                entry["command"] = re.sub(r"(?<!\S)-isystem(?=\s)", "-I", entry["command"])
            else:
                entry["command"] = shlex.join(command)
            # Cppcheck 2.13 does not resolve all -isystem entries from a database.
            entry["arguments"] = ["-I" if item == "-isystem" else item for item in command]
            entries.append(entry)
    if not entries:
        raise ValueError("No SDK translation units in the supplied compilation databases")
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".")
    parser.add_argument("--output", required=True)
    parser.add_argument("databases", nargs="+")
    args = parser.parse_args()
    entries = prepare(args.root, args.databases)
    Path(args.output).write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")
    print(f"Prepared {len(entries)} SDK compilation entries")


if __name__ == "__main__":
    main()
