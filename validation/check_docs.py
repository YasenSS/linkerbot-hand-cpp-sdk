"""Validate local Markdown navigation, language pairs and supported quickstarts."""
import re
import unicodedata
from pathlib import Path
from urllib.parse import unquote
from resolve_release import ci_transport_sha


def prose(text):
    return re.sub(r"```.*?```", "", text, flags=re.S)


def anchors(text):
    text = prose(text)
    result = set(re.findall(r'<a\s+(?:id|name)="([^"]+)"', text))
    counts = {}
    for heading in re.findall(r"^#{1,6}\s+(.+)$", text, re.M):
        heading = re.sub(r"\[([^]]*)\]\([^)]*\)", r"\1", heading).replace("`", "").lower()
        slug = "".join(c for c in heading if c in "-_ " or unicodedata.category(c)[0] in "LN").replace(" ", "-")
        count = counts.get(slug, 0)
        counts[slug] = count + 1
        result.add(slug + (f"-{count}" if count else ""))
    return result


def check(root):
    root = Path(root).resolve()
    files = [*root.glob("*.md"), *root.glob("docs/**/*.md"), *root.glob("tools/*.md"),
             *root.glob("validation/*.md"), *root.glob("tests/**/*.md"), *root.glob(".github/**/*.md")]
    errors = []
    for file in files:
        for target in re.findall(r"\]\(([^)\s]+)", prose(file.read_text(encoding="utf-8"))):
            if "://" in target or target.startswith("mailto:"):
                continue
            path, separator, anchor = unquote(target).partition("#")
            destination = (file.parent / path).resolve() if path else file
            if not destination.exists():
                errors.append(f"{file.relative_to(root)}: missing {target}")
            elif separator and anchor and destination.suffix == ".md" and anchor not in anchors(destination.read_text(encoding="utf-8")):
                errors.append(f"{file.relative_to(root)}: missing anchor {target}")
    for file in (root / "docs/en").rglob("*.md"):
        other = root / "docs/zh" / file.relative_to(root / "docs/en")
        if not other.is_file():
            errors.append(f"Missing Chinese counterpart: {other.relative_to(root)}")
    for file in (root / "docs/zh").rglob("*.md"):
        other = root / "docs/en" / file.relative_to(root / "docs/zh")
        if not other.is_file():
            errors.append(f"Missing English counterpart: {other.relative_to(root)}")
    for default, alias in [("README.md", "README.en.md"), ("docs/README.md", "docs/README.en.md")]:
        if (root / default).read_bytes() != (root / alias).read_bytes():
            errors.append(f"Compatibility alias differs: {alias}")
    for default, chinese in [("README.md", "README.zh-CN.md"), ("CONTRIBUTING.md", "CONTRIBUTING.zh-CN.md"),
                              ("CHANGELOG.md", "CHANGELOG.zh-CN.md"), ("docs/README.md", "docs/README.zh-CN.md"),
                              ("tools/README.md", "tools/README.zh-CN.md")]:
        if Path(chinese).name not in (root / default).read_text(encoding="utf-8"):
            errors.append(f"Missing language switch in {default}")
        if Path(default).name not in (root / chinese).read_text(encoding="utf-8"):
            errors.append(f"Missing language switch in {chinese}")
    pin = ci_transport_sha(root)
    for relative in ["README.md", "README.zh-CN.md", "docs/en/guides/windows.md", "docs/zh/guides/windows.md"]:
        text = (root / relative).read_text(encoding="utf-8")
        if pin not in text:
            errors.append(f"Transport pin drift: {relative}")
        if "linkerbot-hand-cpp-sdk.git linkerbot-hand-cpp" not in text:
            errors.append(f"Noncanonical clone command: {relative}")
        if "--target tests protocol-tests" not in text:
            errors.append(f"Missing explicit offline test targets: {relative}")
    return errors


if __name__ == "__main__":
    errors = check(Path(__file__).resolve().parents[1])
    if errors:
        raise SystemExit("\n".join(errors))
    print("Documentation links, anchors, language pairs, aliases and quickstarts verified")
