# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Build coverage history for the browser regression suite."""

import datetime
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from compact_site import compact
from coverage_index import render_report


def build(root):
    now = datetime.datetime.now(datetime.timezone.utc)
    metric = {'covered': 9, 'total': 10, 'percent': 90}
    names = ('lines', 'functions', 'branches')
    summary = {
        'measurements': {'overall': {name: metric for name in names}},
        'minimums': {'overall': {name: 80 for name in names}},
        'targets': {'overall': {name: 90 for name in names}},
        'enforcement': {'overall': {name: 'medium' for name in names}},
    }
    for target, age in (('runs/2/1', 0), ('pr/12', 0), ('runs/1/1', 8)):
        folder = root / 'coverage' / target
        (folder / 'lcov').mkdir(parents=True)
        (folder / 'coverage-meta.json').write_text(json.dumps({'source': {
            'completed_at': (now - datetime.timedelta(days=age)).isoformat()}}))
        (folder / 'coverage-summary.json').write_text(json.dumps(summary) + '\n')
        (folder / 'index.html').write_text(render_report(summary, target))
        (folder / 'lcov/index.html').write_text(
            '<html><body><a href="file.cc.gcov.html#L100">Source</a></body></html>')
        lines = '\n'.join(f'<span id="L{line}"><span class="lineNum">{line}</span>return {line};</span>'
                          for line in range(1, 121))
        (folder / 'lcov/file.cc.gcov.html').write_text(
            '<html><head><link rel="stylesheet" href="gcov.css"></head><body>'
            '<a href="index.html">Source index</a>' + lines + '</body></html>')
        (folder / 'lcov/gcov.css').write_text('body{color:rgb(1,2,3)}body>span{display:block;height:24px}')
    (root / 'coverage/index.html').write_text('<h1>Coverage history</h1>')
    compact(root, now)
    # A second publisher pass must leave the packed viewer and reports usable.
    compact(root, now)


if __name__ == '__main__':
    build(Path(sys.argv[1]))
