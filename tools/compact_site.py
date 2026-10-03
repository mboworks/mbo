#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Compact the retained publication tree before committing or deploying it."""

import argparse
import datetime
import json
from pathlib import Path
import shutil

import coverage_storage
import site_storage


def sizes(root):
    result = {}
    for path in root.rglob('*'):
        relative = path.relative_to(root)
        if '.git' in relative.parts or relative.as_posix() == 'storage-report.json' or not path.is_file():
            continue
        section = relative.parts[0] if len(relative.parts) > 1 else 'root'
        stats = result.setdefault(section, {'files': 0, 'bytes': 0})
        stats['files'] += 1
        stats['bytes'] += path.stat().st_size
    return result


def compact(root, now):
    coverage_storage.compact(root / 'coverage', now)
    site_storage.compact_summaries(root / 'coverage')
    source = Path(__file__).parent
    for name in ('site_storage', 'site_page'):
        shutil.copyfile(source / (name + '.js'), root / (name.replace('_', '-') + '.js'))
    if (root / 'coverage').is_dir():
        shutil.copyfile(source / 'coverage_viewer.js', root / 'coverage/viewer.js')
        (root / '404.html').write_text(coverage_storage.legacy_router())
    result = sizes(root)
    total = sum(entry['bytes'] for entry in result.values())
    report = {'schema': 1, 'total_bytes': total, 'sections': result,
              'coverage_detail_days': 7, 'review_bytes': 200_000_000}
    (root / 'storage-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2), flush=True)
    if total >= report['review_bytes']:
        print('::warning::Published site reached 200 MB; review storage-report.json and docs/site-storage.md.')
    if total > 9_000_000_000:
        raise ValueError('Published site exceeds the Pages deployment ceiling')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    args = parser.parse_args()
    compact(args.root, datetime.datetime.now(datetime.timezone.utc))


if __name__ == '__main__':
    main()
