# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Regression tests for release snapshots, configured links, and retained history."""

import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import release_site as site


class ReleaseSiteTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.retained = self.root / "retained"
        self.source.mkdir()
        subprocess.run(["git", "init", "-q", str(self.source)], check=True)
        self.write("README.md", "release readme")
        self.write("docs/guide.md", "release guide")
        self.write("image.svg", '<svg xmlns="http://www.w3.org/2000/svg"/>')
        self.config = {"pages": {"README.md": "index.html", "docs/guide.md": "guide/start.html"},
                       "links": [{"label": "Coverage", "href": "/{repo}/coverage/tag/{version}/"}]}
        self.write("release-site.json", json.dumps(self.config))
        subprocess.run(["git", "-C", str(self.source), "add", "."], check=True)
        subprocess.run(["git", "-C", str(self.source), "-c", "user.name=Test",
                        "-c", "user.email=test@example.com", "-c", "commit.gpgsign=false",
                        "commit", "-qm", "fixture"], check=True)

    def write(self, name, text):
        target = self.source / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)

    def render(self, markdown, repository):
        self.assertEqual(repository, "mboworks/mbo")
        if markdown == "release readme":
            return ('<h1>A <code>title</code>!</h1><h1>A title!</h1>'
                    '<a href="docs/guide.md#usage">Guide</a><img src="image.svg">'
                    '<a href="https://github.com/mboworks/mbo/blob/main/docs/guide.md">Guide</a>'
                    '<a href="https://mboworks.github.io/mbo/coverage/main/">Coverage</a>'
                    '<a href="src/code.cc">Source</a>')
        self.assertEqual(markdown, "release guide")
        return '<h1>Usage</h1><a href="../README.md#a-title">Home</a>'

    def build(self, tag="v1.2.3", renderer=None):
        site.build(self.source, self.retained, "mboworks/mbo", tag, renderer or self.render)
        return self.retained / "site/tag" / tag

    def test_configured_destinations_resolve_local_and_github_links(self):
        output = self.build()
        home = (output / "index.html").read_text()
        guide = (output / "guide/start.html").read_text()
        self.assertIn('href="guide/start.html#usage"', home)
        self.assertIn('href="guide/start.html"', home)
        self.assertIn('href="../index.html#a-title"', guide)
        self.assertIn('id="a-title"', home)
        self.assertIn('id="a-title-1"', home)
        self.assertIn('/mbo/coverage/tag/1.2.3/', home)
        self.assertNotIn('/coverage/main/', home)
        sha = site.git(self.source, "rev-parse", "HEAD")
        self.assertIn(f'/blob/{sha}/src/code.cc', home)
        self.assertEqual(len(list((output / "assets").iterdir())), 1)
        self.assertIn('guide/start.html', (output / "documents.html").read_text())

    def test_rerun_retains_original_bytes_without_rendering(self):
        output = self.build()
        before = {p.relative_to(output): p.read_bytes() for p in output.rglob("*") if p.is_file()}
        self.build(renderer=mock.Mock(side_effect=AssertionError("must not render again")))
        self.assertEqual(before, {p.relative_to(output): p.read_bytes()
                                  for p in output.rglob("*") if p.is_file()})

    def test_changed_commit_cannot_replace_retained_release(self):
        self.build()
        with mock.patch.object(site, "git", return_value="different commit"):
            with self.assertRaisesRegex(ValueError, "cannot be replaced"):
                self.build()

    def test_new_release_and_backfill_preserve_coverage_schema_and_history(self):
        for name in ("coverage/tag/1.2.3/index.html", "schema/v1.json", "releases/1.2.3/index.html"):
            target = self.retained / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("retained")
        self.build("1.2.3")
        self.build("2.0.0")
        site.redirect(self.retained, "2.0.0")
        self.build("1.0.0")
        site.redirect(self.retained, "2.0.0")
        self.assertIn('url=site/tag/2.0.0/', (self.retained / "index.html").read_text())
        for name in ("coverage/tag/1.2.3/index.html", "schema/v1.json", "releases/1.2.3/index.html"):
            self.assertEqual((self.retained / name).read_text(), "retained")
        for tag in ("1.0.0", "1.2.3", "2.0.0"):
            self.assertTrue((self.retained / f"site/tag/{tag}/index.html").is_file())

    def test_unpublished_latest_does_not_break_existing_redirect(self):
        self.build()
        site.redirect(self.retained, "v1.2.3")
        before = (self.retained / "index.html").read_bytes()
        site.redirect(self.retained, "9.0.0")
        self.assertEqual((self.retained / "index.html").read_bytes(), before)

    def test_conversion_failure_leaves_no_partial_release(self):
        with self.assertRaisesRegex(RuntimeError, "renderer unavailable"):
            self.build(renderer=mock.Mock(side_effect=RuntimeError("renderer unavailable")))
        self.assertFalse((self.retained / "site/tag/v1.2.3").exists())

    def test_external_images_are_snapshotted(self):
        response = mock.MagicMock()
        response.__enter__.return_value = response
        response.read.return_value = b"image bytes"
        response.headers.get_content_type.return_value = "image/png"
        with mock.patch.object(site, "urlopen", return_value=response):
            output = self.build(renderer=lambda *_: '<img src="https://example.com/live.png">')
        self.assertNotIn('src="https:', (output / "index.html").read_text())
        self.assertEqual(next((output / "assets").iterdir()).read_bytes(), b"image bytes")

    def test_invalid_paths_and_duplicate_destinations_fail(self):
        for pages in ({"README.md": "index.html", "../bad.md": "bad.html"},
                      {"README.md": "index.html", "docs/guide.md": "../bad.html"},
                      {"README.md": "index.html", "docs/guide.md": "index.html"}):
            with self.subTest(pages=pages):
                self.write("release-site.json", json.dumps({"pages": pages}))
                with self.assertRaises(ValueError):
                    self.build()

    def test_configured_files_are_copied_and_links_use_their_destinations(self):
        self.config["files"] = {"image.svg": "static/logo.svg"}
        self.write("release-site.json", json.dumps(self.config))
        output = self.build(renderer=lambda *_: '<a href="/image.svg">Download</a>')
        self.assertEqual((output / "static/logo.svg").read_bytes(),
                         (self.source / "image.svg").read_bytes())
        self.assertIn('href="static/logo.svg"', (output / "index.html").read_text())
        self.assertIn('href="../static/logo.svg"', (output / "guide/start.html").read_text())

    def test_missing_configured_source_fails_before_rendering(self):
        self.config["pages"]["missing.md"] = "missing.html"
        self.write("release-site.json", json.dumps(self.config))
        with self.assertRaisesRegex(ValueError, "Missing"):
            self.build(renderer=mock.Mock(side_effect=AssertionError("must not render")))

    def test_root_relative_coverage_links_select_release(self):
        output = self.build(renderer=lambda *_: '<a href="/mbo/coverage/main/">Report</a>')
        home = (output / "index.html").read_text()
        self.assertIn('href="/mbo/coverage/tag/1.2.3/"', home)
        self.assertNotIn('/coverage/main/', home)

    def test_heading_suffix_collisions_get_unique_anchors(self):
        rendered = site.headings("<h1>Title</h1><h1>Title</h1><h1>Title-1</h1>")
        self.assertIn('id="title"', rendered)
        self.assertIn('id="title-1"', rendered)
        self.assertIn('id="title-1-1"', rendered)

    def test_one_source_cannot_have_page_and_file_destinations(self):
        self.config["files"] = {"README.md": "readme.txt"}
        self.write("release-site.json", json.dumps(self.config))
        with self.assertRaisesRegex(ValueError, "both a page and a file"):
            self.build()

    def test_backfill_uses_old_source_and_retains_exact_override(self):
        override = self.root / "backfill.json"
        data = json.dumps(self.config, indent=4).encode() + b"\n"
        override.write_bytes(data)
        subprocess.run(["git", "-C", str(self.source), "rm", "-q", "release-site.json"], check=True)
        subprocess.run(["git", "-C", str(self.source), "-c", "user.name=Test",
                        "-c", "user.email=test@example.com", "-c", "commit.gpgsign=false",
                        "commit", "-qm", "Historical source without a site config"], check=True)
        site.build(self.source, self.retained, "mboworks/mbo", "v1.2.3",
                   self.render, config_path=override)
        output = self.retained / "site/tag/v1.2.3"
        self.assertEqual((output / "release-site.json").read_bytes(), data)
        metadata = json.loads((output / "release.json").read_text())
        self.assertEqual(metadata["commit"], site.git(self.source, "rev-parse", "HEAD"))
        self.assertEqual(metadata["configuration"]["origin"], "override")
        self.assertEqual(metadata["configuration"]["sha256"], site.hashlib.sha256(data).hexdigest())
        self.assertFalse((self.source / "release-site.json").exists())
        before = (output / "index.html").read_bytes()
        override.write_text("invalid replacement config")
        site.build(self.source, self.retained, "mboworks/mbo", "v1.2.3",
                   mock.Mock(side_effect=AssertionError("must not render")), config_path=override)
        self.assertEqual((output / "index.html").read_bytes(), before)
        self.assertEqual((output / "release-site.json").read_bytes(), data)

    def test_override_never_reads_missing_content_from_publisher_checkout(self):
        override = self.root / "backfill.json"
        self.config["pages"]["newer.md"] = "newer.html"
        override.write_text(json.dumps(self.config))
        (self.root / "newer.md").write_text("content only present alongside publisher config")
        with self.assertRaisesRegex(ValueError, "Missing.*newer.md"):
            site.build(self.source, self.retained, "mboworks/mbo", "v1.2.3",
                       mock.Mock(side_effect=AssertionError("must not render")), config_path=override)

    def test_tag_config_remains_default_and_is_retained(self):
        output = self.build()
        metadata = json.loads((output / "release.json").read_text())
        self.assertEqual(metadata["configuration"]["origin"], "tag")
        self.assertEqual((output / "release-site.json").read_bytes(),
                         (self.source / "release-site.json").read_bytes())

    def test_unmapped_markdown_links_fail_publication(self):
        del self.config["pages"]["docs/guide.md"]
        self.write("release-site.json", json.dumps(self.config))
        for link in ("docs/guide.md", "/docs/guide.md",
                     "https://github.com/mboworks/mbo/blob/main/docs/guide.md"):
            with self.subTest(link=link), self.assertRaisesRegex(ValueError, "no page mapping"):
                self.build(renderer=lambda *_: f'<a href="{link}">Guide</a>')
        self.assertFalse((self.retained / "site/tag/v1.2.3").exists())

    def test_directory_readme_requires_a_mapping(self):
        self.write("docs/README.md", "directory guide")
        with self.assertRaisesRegex(ValueError, "no page mapping"):
            self.build(renderer=lambda *_: '<a href="/docs/">Guide</a>')

    def test_missing_generated_anchor_aborts_without_retaining_snapshot(self):
        with self.assertRaisesRegex(ValueError, "missing generated anchor"):
            self.build(renderer=lambda *_: '<a href="/docs/guide.md#absent">Guide</a>')
        self.assertFalse((self.retained / "site/tag/v1.2.3").exists())

    def test_generated_audit_checks_files_anchors_and_snapshot_boundaries(self):
        output = self.root / "html"
        output.mkdir()
        (output / "guide.html").write_text('<h1 id="usage">Guide</h1>')
        for link, error in (("missing.html", "missing generated link target"),
                            ("guide.html#absent", "missing generated anchor"),
                            ("../outside.html", "escapes release snapshot"),
                            ("/mbo/site/tag/1.2.3/missing.html", "missing generated link target"),
                            ("https://mboworks.github.io/mbo/site/tag/1.2.3/guide.html#absent",
                             "missing generated anchor")):
            with self.subTest(link=link):
                (output / "index.html").write_text(f'<a href="{link}">Target</a>')
                with self.assertRaisesRegex(ValueError, error):
                    site.validate_site(output, "mboworks/mbo", "1.2.3")
        (output / "index.html").write_text(
            '<a href="guide.html#usage">Guide</a><a href="#here">Self</a><a name="here"></a>'
            '<a href="/mbo/coverage/tag/1.2.3/">Coverage</a>'
            '<a href="https://example.com/">External</a>')
        site.validate_site(output, "mboworks/mbo", "1.2.3")

    def test_reserved_config_destination_cannot_be_overwritten(self):
        self.config["files"] = {"image.svg": "release-site.json"}
        self.write("release-site.json", json.dumps(self.config))
        with self.assertRaisesRegex(ValueError, "reserved destination"):
            self.build()

    def test_pages_packaging_exclusions_are_rejected_before_rendering(self):
        for destination in (".github/guide.html", "guide/.hidden.html", ".hidden/guide.html"):
            with self.subTest(destination=destination):
                self.config["pages"]["docs/guide.md"] = destination
                self.write("release-site.json", json.dumps(self.config))
                with self.assertRaisesRegex(ValueError, "excludes hidden"):
                    self.build(renderer=mock.Mock(side_effect=AssertionError("must not render")))
        self.config["pages"]["docs/guide.md"] = "guide.html"
        self.config["files"] = {"image.svg": "assets-custom/.hidden.svg"}
        self.write("release-site.json", json.dumps(self.config))
        with self.assertRaisesRegex(ValueError, "excludes hidden"):
            self.build()

    def test_invalid_tag_cannot_escape_site_directory(self):
        for tag in ("../bad", "v1.2.3/evil", "1.2.3\nextra", "main"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                self.build(tag)

    def test_symlinked_document_is_rejected(self):
        target = self.source / "docs/guide.md"
        target.unlink()
        target.symlink_to(self.source / "README.md")
        with self.assertRaisesRegex(ValueError, "symlinked"):
            self.build()


if __name__ == "__main__":
    unittest.main()
