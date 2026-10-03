#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Lossless storage for generated Pages data; measurements keep their original bytes."""

import gzip


def compact_json(path):
    original = path.read_bytes()
    destination = path.with_suffix(path.suffix + '.gz')
    destination.write_bytes(gzip.compress(original, mtime=0))
    path.unlink()


def compact_page(page, root, data):
    if not page.exists():
        return
    if 'data-packed-page=' not in page.read_text():
        page.with_name('page.html.gz').write_bytes(gzip.compress(page.read_bytes(), mtime=0))
    prefix = '/'.join(['..'] * len(page.parent.relative_to(root).parts))
    page.write_text('<!doctype html><meta charset="utf-8"><title>Report</title>'
                        '<p id="loading" role="status">Loading report...</p>'
                        f'<script src="{prefix}/site-page.js" data-packed-page="page.html.gz"></script>'
                        f'<noscript>This report requires JavaScript. <a href="{data}">Download data</a>.</noscript>\n')


def compact_summaries(root):
    for summary in root.rglob('coverage-summary.json'):
        compact_json(summary)
    for page in root.rglob('index.html'):
        if page.parent.name == 'lcov':
            continue
        text = page.read_text().replace('coverage-summary.json"', 'coverage-summary.json.gz"')
        page.write_text(text)
        if page.with_name('coverage-meta.json').exists():
            compact_page(page, root.parent, 'coverage-summary.json.gz')
