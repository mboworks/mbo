# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Add shared favicons to the staged Pages tree, leaving retained releases unchanged."""

import argparse
import os
from pathlib import Path
import re
import shutil


ICONS = ("favicon.ico", "favicon.png", "apple-touch-icon.png")
MARKER = '<!-- mboworks favicons -->'


def decorate(assets: Path, output: Path) -> None:
    for name in ICONS:
        shutil.copyfile(assets / name, output / name)
    for path in output.rglob("*.html"):
        text = path.read_text(encoding="utf-8")
        if MARKER in text:
            continue
        opening = re.search(r"<head\b[^>]*>", text, re.IGNORECASE)
        if opening is None:
            opening = re.search(r"<html\b[^>]*>", text, re.IGNORECASE)
        if opening is None:
            continue
        prefix = Path(os.path.relpath(output, path.parent)).as_posix()
        links = (f'\n{MARKER}\n'
                 f'<link rel="icon" href="{prefix}/favicon.ico">\n'
                 f'<link rel="icon" type="image/png" sizes="32x32" '
                 f'href="{prefix}/favicon.png">\n'
                 f'<link rel="apple-touch-icon" sizes="180x180" '
                 f'href="{prefix}/apple-touch-icon.png">\n')
        path.write_text(text[:opening.end()] + links + text[opening.end():], encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("assets", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    decorate(args.assets, args.output)


if __name__ == "__main__":
    main()
