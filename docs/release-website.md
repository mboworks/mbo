<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Release website

Release notes use [`.github/release-notes.md.template`](../.github/release-notes.md.template), rendered by
[`tools/release_notes.sh TAG`](../tools/release_notes.sh), to link to that tag's versioned website and related
release resources. The existing changelog and installation notes remain included.

The [website](https://mboworks.github.io/mbo/) forwards to the latest published
stable release at `site/tag/<tag>/`, preserving the exact Git tag name.
Each release keeps its converted HTML, images, and configured files. Retrying
publication leaves an existing snapshot unchanged; a different commit cannot
replace it. Older versions remain directly accessible.

[`release-site.json`](../release-site.json) defines the layout. Source names are
relative to the repository root; destinations are relative to that release's
site directory. For example:

```json
{
  "pages": {
    "README.md": "index.html",
    "docs/guide.md": "guide/index.html"
  },
  "files": {
    "schema/example.json": "schema/v1.json"
  },
  "links": [
    {
      "label": "Release",
      "href": "https://github.com/{owner}/{repo}/releases/tag/{tag}"
    }
  ]
}
```

Use existing source files in the actual configuration. `pages` converts Markdown;
optional `files` copies other files unchanged. `README.md` must map to `index.html`.
The generated `documents.html`, `release.json`, `release-site.json`, and `assets/`
paths are reserved. Destination paths cannot have hidden components (names starting
with a dot), because the Pages artifact uploader excludes them. Hidden source
paths remain valid; for example, `.github/workflows/README.md` maps to
`workflows/index.html`.
Navigation links support `{owner}`, `{repo}`, `{tag}`, `{version}`, and `{commit}`.
`{version}` omits a leading `v` for compatibility with coverage report paths.
By default, the configuration and content come from the release tag. Every linked
local Markdown page (including directory README links) must have a `pages` mapping.
Publication fails for an omitted mapping, a missing generated file, or a broken
anchor within the snapshot. Links to configured pages follow their destination
mappings; other local source links use the exact release commit. Embedded images are copied, including remote badges. Markdown
conversion uses the [GitHub Markdown API](https://docs.github.com/en/rest/markdown/markdown)
at publication time; browsing the result requires no Markdown renderer or CDN.

After the Release workflow succeeds, `Publish release site` retains the snapshot
on `coverage-pages` and deploys the complete Pages tree. Coverage and site
publication share a concurrency group to preserve both trees. GitHub's latest
stable release selects the root redirect; backfilling an older release does not
make it latest. The workflow can also be dispatched with a published tag to retry
publication. Enable GitHub Pages with
**GitHub Actions** as its source, and set the repository's About website to
`https://mboworks.github.io/mbo/`.

## Backfill a historical release

No new release or tag change is needed. Manually dispatch `Publish release site`
with `tag` set to the historical release and `config_path` set to a tracked JSON
file on `main`. For `0.15.0`, use the current `release-site.json`:

```sh
gh workflow run pages.yml --repo mboworks/mbo --ref main \
  -f tag=0.15.0 -f config_path=release-site.json
```

The override changes only the publication layout; all Markdown and copied files
still come from the selected tag. A configuration can serve multiple historical
tags when its sources exist in each tag. For a different historical layout, add
another configuration on `main` and select its repository-relative path. Missing
sources or links fail publication instead of falling back to newer content.
Automatic release publication continues to use the configuration from the tag.

Each new snapshot retains the exact configuration as `release-site.json` and
records its SHA-256, whether it came from the tag or an override, and the source
commit in `release.json`. Retrying a published tag does not replace its HTML or
configuration, even if the selected override has changed since publication.

To verify a backfill locally without deploying, check out the historical tag in
`/tmp/mbo-0.15.0` and run from the current publisher checkout:

```sh
python3 tools/release_site.py /tmp/mbo-0.15.0 /tmp/mbo-site-preview \
  --repository mboworks/mbo --tag 0.15.0 --latest 0.15.0 \
  --config release-site.json
```

Local regression tests: `python3 -m unittest discover -s tools -p release_site_test.py`.
CI also converts the configured documentation and checks the generated links in
a disposable runner directory. It never commits, retains, or deploys that preview.

Release coverage links select `https://mboworks.github.io/mbo/coverage/tag/<version>/`,
matching the coverage publisher rather than the moving main-branch report.
