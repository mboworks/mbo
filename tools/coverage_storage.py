#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Keep seven days of source coverage in shared compressed report archives."""

import base64
import datetime
import difflib
import gzip
import hashlib
import html
import json
from pathlib import Path
import re
import shutil
from urllib.parse import urlencode


def pack_report(files, assets, bases):
    """Store each file against one shared base, never a chain of earlier reports."""
    result = {}
    for name, (mime, content) in files.items():
        numbered = False
        line_pattern = r'<span id="L(\d+)"><span class="lineNum">\s*\d+</span>'
        numbers = re.findall(line_pattern, content) if name.endswith('.gcov.html') else []
        if numbers and list(map(int, numbers)) == list(range(1, len(numbers) + 1)):
            content = re.sub(line_pattern, '<span data-source-line><span class="lineNum">@LINE@</span>', content)
            numbered = True
        digest = bases.get(name)
        baseline = assets / 'bases' / (str(digest) + '.json.gz')
        if digest and baseline.exists() and numbered:
            base = json.loads(gzip.decompress(baseline.read_bytes()))
            if re.search(line_pattern, base):
                # Upgrade a prior unnumbered base without changing reports still using it.
                digest = None
        if not digest or not baseline.exists():
            digest = hashlib.sha256(content.encode()).hexdigest()
            baseline = assets / 'bases' / (digest + '.json.gz')
            baseline.parent.mkdir(parents=True, exist_ok=True)
            baseline.write_bytes(gzip.compress(json.dumps(content).encode(), mtime=0))
            bases[name] = digest
            base = content
        else:
            base = json.loads(gzip.decompress(baseline.read_bytes()))
        record = {'base': digest}
        if numbered:
            record['numbered'] = True
        if content != base:
            left, right = base.splitlines(keepends=True), content.splitlines(keepends=True)
            edits = []
            for operation, first, last, begin, end in difflib.SequenceMatcher(None, left, right).get_opcodes():
                if operation == 'equal':
                    edits.append([first, last])
                elif begin != end:
                    edits.append(''.join(right[begin:end]))
            record['edits'] = edits
        result[name] = [mime, record]
    return {'schema': 1, 'files': result}


def compact(root, now):
    if not root.exists():
        return
    threshold = now - datetime.timedelta(days=7)
    assets = root / 'assets/reports'
    assets.mkdir(parents=True, exist_ok=True)
    base_index = assets / 'bases.json'
    bases = json.loads(base_index.read_text()) if base_index.exists() else {}
    used = set()
    used_bases = set()
    inspected = set()
    for metadata in sorted(root.rglob('coverage-meta.json')):
        report = metadata.parent
        target = report.relative_to(root).as_posix()
        source = json.loads(metadata.read_text())['source']
        date = source.get('completed_at') or source.get('created_at')
        # Undated legacy reports cannot qualify for a recent-details retention window.
        recent = date and datetime.datetime.fromisoformat(date.replace('Z', '+00:00')) >= threshold
        descriptor = report / 'details.json'
        details = json.loads(descriptor.read_text()) if descriptor.exists() else {}
        lcov = report / 'lcov'
        if recent and lcov.is_dir() and not details:
            files = {}
            for path in sorted(lcov.rglob('*')):
                if not path.is_file() or path.is_symlink():
                    continue
                name = path.relative_to(lcov).as_posix()
                mime = {'.html': 'text/html', '.css': 'text/css', '.png': 'image/png'}.get(path.suffix)
                if mime is None:
                    continue
                files[name] = [mime, (base64.b64encode(path.read_bytes()).decode('ascii')
                                     if mime.startswith('image/') else path.read_text())]
            if files:
                raw = json.dumps(pack_report(files, assets, bases), separators=(',', ':')).encode()
                digest = hashlib.sha256(raw).hexdigest()
                archive = assets / (digest + '.json.gz')
                if not archive.exists():
                    archive.write_bytes(gzip.compress(raw, mtime=0))
                details = {'archive': archive.relative_to(root).as_posix(), 'retention_days': 7}
        if not recent:
            details = {'retention_days': 7, 'reason': 'Detailed source coverage is retained for seven days. '
                       'The summary and original run metadata remain available.'}
        elif not details:
            details = {'retention_days': 7, 'reason': 'This report did not include detailed source coverage.'}
        if 'archive' in details:
            if not re.fullmatch(r'assets/reports/[a-f0-9]{64}\.json\.gz', details['archive']):
                raise ValueError('invalid coverage archive path')
            archive = root / details['archive']
            if not archive.is_file() or not archive.is_relative_to(assets):
                raise ValueError(f'missing or invalid coverage archive: {archive}')
            if archive not in inspected:
                stored = json.loads(gzip.decompress(archive.read_bytes()))
                if stored['schema'] != 1:
                    raise ValueError('unsupported coverage archive schema')
                inspected.add(archive)
                for _, record in stored['files'].values():
                    digest = record['base']
                    if not re.fullmatch(r'[a-f0-9]{64}', digest):
                        raise ValueError('invalid coverage base digest')
                    if not (assets / 'bases' / (digest + '.json.gz')).is_file():
                        raise ValueError(f'missing coverage base: {digest}')
                    used_bases.add(digest)
            used.add(archive)
        descriptor.write_text(json.dumps(details, separators=(',', ':')) + '\n')
        # The old report entry point remains valid; deep links use the shared site 404 router.
        if lcov.exists():
            shutil.rmtree(lcov)
        lcov.mkdir()
        viewer = '../' * (len(report.relative_to(root).parts) + 1) + 'view.html?' + urlencode({'report': target})
        lcov.joinpath('index.html').write_text(
            '<!doctype html><meta charset="utf-8"><title>Coverage details</title>'
            f'<meta http-equiv="refresh" content="0;url={html.escape(viewer, quote=True)}">'
            f'<a href="{html.escape(viewer, quote=True)}">Open coverage details</a>\n')
    for path in assets.glob('*.json.gz'):
        if path not in used:
            path.unlink()
    for path in (assets / 'bases').glob('*.json.gz'):
        if path.name.removesuffix('.json.gz') not in used_bases:
            path.unlink()
    base_index.write_text(json.dumps({name: digest for name, digest in bases.items() if digest in used_bases}))
    root.joinpath('view.html').write_text(
        '<!doctype html><html lang="en"><meta charset="utf-8"><title>Coverage details</title>'
        '<link rel="icon" href="../favicon.ico">'
        '<meta name="viewport" content="width=device-width,initial-scale=1">'
        '<style>body{font:16px system-ui;margin:1rem}iframe{width:100%;height:90vh;border:0}</style>'
        '<p><a id="summary">Report summary</a> | <a href="./">Coverage history</a></p>'
        '<p id="status" role="status">Loading coverage details...</p>'
        '<iframe title="Source coverage" hidden sandbox="allow-same-origin allow-top-navigation-by-user-activation"></iframe>'
        '<script src="../site-storage.js"></script><script src="viewer.js"></script></html>\n')


def legacy_router():
    return r'''<!doctype html><meta charset="utf-8"><title>Page not found</title>
<h1>Page not found</h1><p>The requested page is unavailable.</p>
<script>
const match=location.pathname.match(/^(.*\/coverage\/)((?:main|pr\/\d+|tag\/[^/]+|runs\/\d+\/\d+))\/lcov\/(.*)$/);
if(match)location.replace(match[1]+"view.html?"+new URLSearchParams({report:match[2],file:decodeURIComponent(match[3])||"index.html"})+location.hash);
</script>
'''
