"""Run Cppcheck for the Linux report without changing Clang's compile arguments."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def command_database(entries):
    result = []
    for original in entries:
        entry = dict(original)
        if not entry.get("command"):
            raise ValueError("Cppcheck report requires shell-escaped command entries")
        entry.pop("arguments", None)
        result.append(entry)
    return result


def main():
    args = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="hand-cppcheck-") as temporary:
        for index, argument in enumerate(args):
            if argument.startswith("--project="):
                source = Path(argument.split("=", 1)[1])
                destination = Path(temporary) / "compile_commands.json"
                destination.write_text(json.dumps(command_database(json.loads(source.read_text()))))
                args[index] = "--project=" + str(destination)
        library = Path(__file__).with_name("catch2-cppcheck.cfg")
        # This wrapper belongs to the Ubuntu report. Cppcheck does not import
        # compiler built-ins from compile_commands.json; select the real target.
        command = ["cppcheck", "--platform=unix64", "-D__unix__=1", "-D__linux__=1",
                   "-U_WIN32", "-U__APPLE__", "--library=std", "--library=" + str(library),
                   "--suppress=missingIncludeSystem", *args]
        return subprocess.run(command, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
