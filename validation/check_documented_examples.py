"""Compile complete C++ examples from the public guides against an installed SDK.

Only generates build-directory sources; no example is run on hardware.
"""
import argparse
import re
import subprocess
from pathlib import Path
from build_support import add_build_arguments, cmake_options

parser = argparse.ArgumentParser()
parser.add_argument("--prefix", required=True, type=Path)
parser.add_argument("--build-dir", required=True, type=Path)
add_build_arguments(parser)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
documents = {
    "readme": "README.md",
    "readme-en": "README.en.md",
}
for language in ("zh", "en"):
    for document, relative in {
        "windows": "guides/windows.md",
        "usage": "guides/usage.md",
        "model-capabilities": "reference/model-capabilities.md",
        "windows-adapter": "connections/windows-adapters.md",
    }.items():
        documents[f"{language}-{document}"] = f"docs/{language}/{relative}"
units = {}
for document, relative in documents.items():
    snippets = re.findall(r"```cpp\n(.*?)```", (root / relative).read_text(encoding='utf-8'), re.S)
    if not snippets:
        raise RuntimeError(f"No C++ examples found in {relative}")
    for number, code in enumerate(snippets, 1):
        if not re.search(r"\bint\s+main\s*\(", code):
            raise RuntimeError(f"Example {number} in {relative} must be a complete program")
        units[f"{document}-{number}"] = code
args.build_dir.mkdir(parents=True, exist_ok=True)
for name, code in units.items():
    (args.build_dir / f"{name}.cpp").write_text(code, encoding='utf-8')
(args.build_dir / "CMakeLists.txt").write_text(
    'cmake_minimum_required(VERSION 3.16)\nproject(documented-examples LANGUAGES CXX)\n'
    'find_package(linkerbot-hand-cpp 0.3 CONFIG REQUIRED)\n'
    'if(MSVC)\n  add_compile_options(/utf-8)\nendif()\n' +
    ''.join(f'add_executable({name} {name}.cpp)\ntarget_link_libraries({name} PRIVATE linkerbot::cpp-sdk)\n'
            for name in units), encoding='utf-8')
subprocess.run(["cmake", "-S", str(args.build_dir), "-B", str(args.build_dir / "build"),
                f"-DCMAKE_PREFIX_PATH={args.prefix.resolve()}", *cmake_options(args)], check=True)
subprocess.run(["cmake", "--build", str(args.build_dir / "build"), "--config", args.config,
                "--parallel", "2"], check=True)
print(f"{len(units)} documented C++ examples compiled (not run on hardware)")
