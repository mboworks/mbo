# Published site storage

Coverage, release documentation, and shared assets use one complete-site storage budget.
`storage-report.json` records byte totals and file counts by top-level directory. Publication warns
at **200 MB** so growth can be reviewed early. The final payload guard is **9 GB**, allowing some TAR
packaging headroom below the uploader's absolute limit. GitHub's documented supported published-site
limit remains [1 GB](https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits);
the emergency guard is not a supported operating budget.

This adopts the coverage storage design from
[xff PR #955](https://github.com/mboworks/xff/pull/955), merged as
`91064c283fa90445db48ec598e4e052fb9d37261`.

## Retention and representation

- Coverage summaries, category totals, patch results, policy thresholds, original run metadata,
  and pre/post-merge attribution are retained indefinitely. No report-count limit evicts history.
- Detailed source, function, and branch pages expire **seven days** after the report's original
  completion timestamp. Replays do not renew that timestamp. Undated legacy reports have no
  recent-detail window. Their summaries and metadata remain available.
- Older LCOV-only directories without structured summaries or metadata are preserved intact.
  They cannot enter the retention scheme until their aggregate data and identity can be recovered;
  the migration does not invent either or discard the only historical report. Their bytes still
  count toward the site budget.
- Recent details use compressed manifests and shared source-page bases. Changed pages store edits
  against one base; there are no dependency chains through earlier reports. The viewer generates
  source line numbers from the ordered lines so inserting a line does not duplicate subsequent
  numbered markup. Collection removes only detail archives and bases without a retained reference.
- Summary JSON is gzip-compressed without changing its original bytes. Report landing pages keep
  their URLs and load compressed HTML through a shared loader. Old LCOV source URLs route through
  the site's 404 document to the viewer, preserving report identity and line anchors. Expired
  details explain the policy and link their aggregate report.
- The browser uses built-in `DecompressionStream`; packed views require JavaScript. Raw
  `coverage-summary.json.gz` and uncompressed `coverage-meta.json` remain downloadable without it.
- Release documentation, benchmark data, coverage thresholds, and measurement identities are not
  changed by compaction.

## Publication

Both coverage and release-site publishers compact the retained `coverage-pages` tree **before
committing**. Each then stages the complete site, installs deployment-only artwork, compacts again,
and measures the final payload before upload. Every publisher uses the existing serialized queue.
Successful coverage jobs are recognized under both the direct `coverage` and reusable
`coverage / test` job names; unrelated successful jobs cannot authorize coverage ingestion.

Publication checkouts fetch only the current retained snapshot. The coverage source checkout keeps
commit ancestry with `blob:none` filtering, and release restoration explicitly fetches only the
shallow publication branch. Compaction does not rewrite Git history: earlier committed source
details remain in old commits. Deployed bytes, Git object storage, Actions artifacts, and build
caches are separate budgets.

## Validation and future growth

Python tests cover summary bytes, metadata, expiry boundaries, shared-base collection, missing
bases, replay behavior, repeated compaction, complete-site size accounting, and both publishers.
Chromium tests exercise packed summary tables, compressed JSON, source navigation, CSS, line
anchors, old deep links, expired reports, and the download fallback without JavaScript:

```sh
python3 -m unittest discover -s tools -p '*_test.py'
cd tools/coverage_web
npm ci
npx playwright install chromium
npm test
```

The same browser suite runs in the required release-site CI job. To use an installed Chromium
browser locally, set `CHROME_PATH` to its executable when running `npm test`.

At 200 MB, inspect the section totals and consider shared data-driven summary tables and a more
compact source representation. Preserve aggregate measurements and provenance. Moving historical
data to other storage or rewriting publication Git history requires a separate decision.
