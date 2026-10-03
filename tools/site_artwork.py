# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Add shared favicons to the staged Pages tree, leaving retained releases unchanged."""

import argparse
import gzip
import itertools
import os
from pathlib import Path
import re
import shutil


ICONS = ("favicon.ico", "favicon.png", "apple-touch-icon.png")
MARKER = '<!-- mboworks favicons -->'


def decorate(assets: Path, output: Path) -> None:
    for name in ICONS:
        shutil.copyfile(assets / name, output / name)
    for path in itertools.chain(output.rglob("*.html"), output.rglob("*.html.gz")):
        compressed = path.suffix == '.gz'
        text = (gzip.decompress(path.read_bytes()).decode('utf-8') if compressed
                else path.read_text(encoding="utf-8"))
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
        decorated = text[:opening.end()] + links + text[opening.end():]
        if compressed:
            path.write_bytes(gzip.compress(decorated.encode('utf-8'), mtime=0))
        else:
            path.write_text(decorated, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("assets", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    decorate(args.assets, args.output)


if __name__ == "__main__":
    main()
