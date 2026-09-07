#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Convert a release checkout to a retained, self-contained documentation site."""

import argparse
import hashlib
import html
from html.parser import HTMLParser
import json
from pathlib import Path
import posixpath
import re
import subprocess
import tempfile
from urllib.parse import quote, unquote, urlsplit, urlunsplit
from urllib.request import urlopen


STYLE = """
:root { color-scheme: light dark; font: 17px/1.6 system-ui, sans-serif; }
body { max-width: 76rem; margin: auto; padding: 2rem; }
a { color: light-dark(#075da8, #8cc8ff); }
nav { display: flex; flex-wrap: wrap; gap: 1rem; border-bottom: 1px solid #888; }
pre { padding: 1rem; overflow: auto; background: light-dark(#f3f5f7, #20252b); }
code { font-size: .9em; } img { max-width: 100%; }
table { display: block; overflow: auto; border-collapse: collapse; }
th, td { border: 1px solid #888; padding: .4rem .7rem; }
blockquote { border-left: 4px solid #888; margin-left: 0; padding-left: 1rem; }
"""


def git(source, *args):
    return subprocess.check_output(["git", "-C", str(source), *args], text=True).strip()


def render(markdown, repository):
    return subprocess.check_output(
        ["gh", "api", "markdown", "--input", "-"],
        input=json.dumps({"text": markdown, "mode": "gfm", "context": repository}),
        text=True,
    )


def version(tag):
    if not re.fullmatch(r"v?[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?", tag):
        raise ValueError(f"Invalid release tag: {tag!r}")
    return tag.removeprefix("v")


def configuration(source, override=None):
    data = (override if override is not None else source / "release-site.json").read_bytes()
    config = json.loads(data)
    pages = config["pages"]
    if not pages or pages.get("README.md") != "index.html":
        raise ValueError("README.md must map to index.html")
    destinations = set()
    for src, dst in pages.items():
        for path in (src, dst):
            if (not isinstance(path, str) or path.startswith("/")
                    or ".." in Path(path).parts or str(Path(path)) != path
                    or any(char in path for char in "\\?#")):
                raise ValueError(f"Unsafe site path: {path!r}")
        if not src.endswith(".md") or not dst.endswith(".html"):
            raise ValueError("Page mappings must convert .md sources to .html destinations")
        if dst in destinations or dst == "documents.html" or dst.startswith("assets/"):
            raise ValueError(f"Duplicate or reserved destination: {dst}")
        if any(part.startswith(".") for part in Path(dst).parts):
            raise ValueError(f"Pages excludes hidden destinations: {dst}")
        destinations.add(dst)
    for src, dst in config.get("files", {}).items():
        if src in pages:
            raise ValueError(f"Source is mapped as both a page and a file: {src}")
        if src.lower().endswith(".md"):
            raise ValueError(f"Markdown must be converted through pages: {src}")
        for path in (src, dst):
            if (not isinstance(path, str) or path.startswith("/")
                    or ".." in Path(path).parts or str(Path(path)) != path
                    or any(char in path for char in "\\?#")):
                raise ValueError(f"Unsafe asset path: {path!r}")
        if dst in destinations or dst in ("documents.html", "release.json", "release-site.json") or dst.startswith("assets/"):
            raise ValueError(f"Duplicate or reserved destination: {dst}")
        if any(part.startswith(".") for part in Path(dst).parts):
            raise ValueError(f"Pages excludes hidden destinations: {dst}")
        destinations.add(dst)
    return config, data


def headings(body):
    """The Markdown API omits GitHub's heading anchors; restore their slugs."""
    used = set()

    def heading(match):
        level, attrs, text = match.groups()
        plain = html.unescape(re.sub(r"<[^>]*>", "", text)).lower()
        slug = re.sub(r"[^\w\- ]", "", plain).replace(" ", "-")
        anchor = slug
        count = 0
        while anchor in used:
            count += 1
            anchor = f"{slug}-{count}"
        used.add(anchor)
        return f'<h{level}{attrs} id="{html.escape(anchor, quote=True)}">{text}</h{level}>'

    return re.sub(r"<h([1-6])([^>]*)>(.*?)</h\1>", heading, body, flags=re.DOTALL)


class Links(HTMLParser):
    def __init__(self, source, output, document, documents, repository, sha, tag):
        super().__init__(convert_charrefs=False)
        self.source = source
        self.output = output
        self.document = document
        self.documents = documents
        self.repository = repository
        self.sha = sha
        self.tag = tag
        self.parts = []

    def local(self, value):
        parsed = urlsplit(value)
        prefix = f"https://github.com/{self.repository}/"
        if value.startswith(prefix):
            rest = value[len(prefix):]
            for kind in ("blob/", "tree/"):
                for ref in ("main", "master", self.tag, self.sha):
                    start = f"{kind}{ref}/"
                    if rest.startswith(start):
                        return posixpath.normpath(unquote(urlsplit(rest[len(start):]).path))
        if parsed.scheme or parsed.netloc or not parsed.path:
            return None
        path = unquote(parsed.path)
        if path.startswith("/"):
            return posixpath.normpath(path.lstrip("/"))
        return posixpath.normpath(posixpath.join(posixpath.dirname(self.document), path))

    def rewrite(self, value, image=False):
        parsed = urlsplit(value)
        owner, repo = self.repository.split("/")
        coverage = f"/{repo}/coverage/"
        if not image and (value.startswith(coverage) or value.startswith(f"https://{owner}.github.io{coverage}")):
            return f"/{repo}/coverage/tag/{version(self.tag)}/"
        local = self.local(value)
        if local is not None:
            if local == ".." or local.startswith("../"):
                raise ValueError(f"Link escapes repository: {value}")
            readme = f"{local.rstrip('/')}/README.md"
            if local not in self.documents and (readme in self.documents or (self.source / readme).is_file()):
                local = readme
            if local == ".":
                local = "README.md"
            if local in self.documents and not image:
                target = posixpath.relpath(self.documents[local], posixpath.dirname(self.documents[self.document]) or ".")
                return urlunsplit(("", "", quote(target), parsed.query, parsed.fragment))
            if not image and local.lower().endswith(".md"):
                raise ValueError(f"{self.document}: linked Markdown has no page mapping: {local}")
            path = self.source / local
            if image:
                if not path.is_file() or path.resolve() != path.absolute():
                    raise ValueError(f"Missing or symlinked image: {local}")
                data = path.read_bytes()
                suffix = path.suffix
            else:
                kind = "tree" if path.is_dir() else "blob"
                return f"https://github.com/{self.repository}/{kind}/{self.sha}/{quote(local)}" + (
                    f"#{parsed.fragment}" if parsed.fragment else ""
                )
        elif image:
            if parsed.scheme not in ("https", "http"):
                raise ValueError(f"Unsupported image URL: {value}")
            with urlopen(value, timeout=60) as response:
                data = response.read()
                media = response.headers.get_content_type()
            suffix = {"image/svg+xml": ".svg", "image/png": ".png", "image/jpeg": ".jpg",
                      "image/gif": ".gif", "image/webp": ".webp"}.get(media)
            if suffix is None:
                raise ValueError(f"Unsupported image content type: {media}")
        else:
            return value
        asset = f"assets/{hashlib.sha256(data).hexdigest()}{suffix}"
        (self.output / "assets").mkdir(exist_ok=True)
        (self.output / asset).write_bytes(data)
        return posixpath.relpath(asset, posixpath.dirname(self.documents[self.document]) or ".")

    def handle_starttag(self, tag, attrs):
        rewritten = []
        for key, value in attrs:
            if key in ("data-canonical-src", "srcset"):
                continue
            if value is not None and key in ("href", "src"):
                value = self.rewrite(value, image=tag == "img" and key == "src")
            rewritten.append(key if value is None else f'{key}="{html.escape(value, quote=True)}"')
        self.parts.append(f"<{tag}{' ' if rewritten else ''}{' '.join(rewritten)}>")

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)

    def handle_endtag(self, tag):
        self.parts.append(f"</{tag}>")

    def handle_data(self, data):
        self.parts.append(data)

    def handle_entityref(self, name):
        self.parts.append(f"&{name};")

    def handle_charref(self, name):
        self.parts.append(f"&#{name};")


class PageReferences(HTMLParser):
    """Collect browser-visible anchors and links from final HTML."""

    def __init__(self, text):
        super().__init__()
        self.anchors = set()
        self.links = []
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        for key, value in attrs:
            if value is None:
                continue
            if key == "id" or (tag == "a" and key == "name"):
                self.anchors.add(value)
            if key in ("href", "src"):
                self.links.append(value)

    handle_startendtag = handle_starttag


def validate_site(output, repository, tag):
    """Reject broken links inside the snapshot before retaining or deploying it."""
    owner, repo = repository.split("/")
    base = f"/{repo}/site/tag/{tag}/"
    host = f"{owner}.github.io"
    pages = {path.relative_to(output).as_posix(): PageReferences(path.read_text())
             for path in output.rglob("*.html")}
    for document, page in pages.items():
        for link in page.links:
            parsed = urlsplit(link)
            if parsed.scheme or parsed.netloc:
                if parsed.scheme not in ("http", "https") or parsed.netloc != host:
                    continue
                if not parsed.path.startswith(base):
                    continue
            path = unquote(parsed.path)
            if path.startswith("/"):
                if not path.startswith(base):
                    continue  # Coverage and other explicitly separate Pages trees.
                target = posixpath.normpath(path[len(base):])
            elif path:
                target = posixpath.normpath(posixpath.join(posixpath.dirname(document), path))
            else:
                target = document
            if target == ".." or target.startswith("../"):
                raise ValueError(f"{document}: link escapes release snapshot: {link}")
            destination = output / target
            if destination.is_dir():
                target = posixpath.join(target, "index.html")
                destination = output / target
            target = posixpath.normpath(target)
            if not destination.is_file():
                raise ValueError(f"{document}: missing generated link target: {link}")
            fragment = unquote(parsed.fragment)
            if fragment and target in pages and fragment not in pages[target].anchors:
                raise ValueError(f"{document}: missing generated anchor: {link}")


def build(source, retained, repository, tag, renderer=render, config_path=None):
    source = source.resolve()
    release_version = version(tag)
    destination = retained / "site" / "tag" / tag
    sha = git(source, "rev-parse", "HEAD")
    if destination.exists():
        metadata = json.loads((destination / "release.json").read_text())
        if metadata["commit"] != sha or metadata["tag"] != tag:
            raise ValueError("A retained release cannot be replaced by a different commit or tag")
        return
    config, config_data = configuration(source, config_path)
    documents = config["pages"]
    files = config.get("files", {})
    tracked = set(git(source, "ls-files", "-z").split("\0"))
    for document in [*documents, *files]:
        path = source / document
        if document not in tracked or not path.is_file() or path.resolve() != path.absolute():
            raise ValueError(f"Missing, untracked, or symlinked document: {document}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
        output = Path(temporary)
        for src, dst in files.items():
            target = output / dst
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((source / src).read_bytes())
        owner, repo = repository.split("/")
        base = f"/{repo}/site/tag/{tag}/"
        links = [("Home", base), ("Documentation", base + "documents.html"),
                 ("Release & downloads", f"https://github.com/{repository}/releases/tag/{tag}"),
                 ("Source", f"https://github.com/{repository}/tree/{sha}")]
        for link in config.get("links", []):
            links.append((link["label"], link["href"].format(
                repo=repo, owner=owner, tag=tag, version=release_version, commit=sha)))
        navigation = "".join(f'<a href="{html.escape(url)}">{html.escape(label)}</a>' for label, url in links)

        def page(title, body):
            return (f'<!doctype html><html lang="en"><meta charset="utf-8">'
                    f'<meta name="viewport" content="width=device-width, initial-scale=1">'
                    f'<title>{html.escape(title)} - {repo} {tag}</title><style>{STYLE}</style>'
                    f'<body><nav>{navigation}</nav><p>{repo} {tag}</p><main>{body}</main>'
                    f'<footer>Release snapshot · {sha}</footer></body></html>\n')

        for document in sorted(documents):
            parser = Links(source, output, document, {**documents, **files}, repository, sha, tag)
            parser.feed(headings(renderer((source / document).read_text(), repository)))
            target = output / documents[document]
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(page(document, "".join(parser.parts)))
        index = "<h1>Documentation</h1><ul>" + "".join(
            f'<li><a href="{quote(documents[path])}">{html.escape(path)}</a></li>'
            for path in sorted(documents)
        ) + "</ul>"
        (output / "documents.html").write_text(page("Documentation", index))
        (output / "release-site.json").write_bytes(config_data)
        (output / "release.json").write_text(json.dumps({
            "tag": tag, "commit": sha,
            "configuration": {"origin": "override" if config_path is not None else "tag",
                              "sha256": hashlib.sha256(config_data).hexdigest()},
        }) + "\n")
        validate_site(output, repository, tag)
        # Rename only after every document and image has been converted successfully.
        output.rename(destination)


def redirect(retained, tag):
    version(tag)  # Validate the exact Git tag before using it as a path.
    target = f"site/tag/{tag}/"
    if not (retained / target / "index.html").is_file():
        return  # An older backfill must not redirect to an unpublished latest site.
    (retained / "index.html").write_text(
        '<!doctype html><html lang="en"><meta charset="utf-8">'
        f'<meta http-equiv="refresh" content="0; url={target}">'
        f'<title>Latest release</title><a href="{target}">Latest release</a></html>\n'
    )
    (retained / ".nojekyll").touch()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("retained", type=Path)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--latest", required=True)
    parser.add_argument("--config", type=Path, help="Explicit configuration override for historical tags")
    args = parser.parse_args()
    build(args.source, args.retained, args.repository, args.tag, config_path=args.config)
    if args.latest:
        redirect(args.retained, args.latest)


if __name__ == "__main__":
    main()
