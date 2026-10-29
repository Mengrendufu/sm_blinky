#!/usr/bin/env python3
"""Embed the local SVG sources into an offline UML viewer."""
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def main():
    page = (ROOT / "template.html").read_text()
    for token, name in [
        ("{{COMPONENTS}}", "components.svg"),
        ("{{OBJECTS}}", "objects.svg"),
    ]:
        page = page.replace(token, (ROOT / name).read_text().rstrip())
    target = ROOT / "ui-components.html"
    target.write_text(page)
    print(target)


if __name__ == "__main__":
    main()
