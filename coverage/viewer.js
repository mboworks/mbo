// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
(async () => {
  const status = document.getElementById("status");
  try {
    const query = new URLSearchParams(location.search);
    const report = query.get("report");
    if (
      !/^(main|pr\/\d+|tag\/[^/.]+\.[^/.]+\.[^/.]+|runs\/\d+\/\d+)$/.test(
        report,
      )
    )
      throw new Error("Invalid report path");
    document.getElementById("summary").href = report + "/";
    const response = await fetch(report + "/details.json");
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const details = await response.json();
    if (!details.archive) {
      status.textContent = details.reason;
      return;
    }
    if (!/^assets\/reports\/[a-f0-9]{64}\.json\.gz$/.test(details.archive))
      throw new Error("Invalid archive path");
    const archive = await window.MboSiteStorage.gzip(details.archive);
    const cache = new Map();
    async function content(entry) {
      const record = entry[1];
      if (!/^[a-f0-9]{64}$/.test(record.base))
        throw new Error("Invalid source base");
      if (!cache.has(record.base))
        cache.set(
          record.base,
          window.MboSiteStorage.gzip(
            `assets/reports/bases/${record.base}.json.gz`,
          ),
        );
      const base = await cache.get(record.base);
      const lines = base.match(/[^\n]*\n|[^\n]+$/g) || [];
      let text = record.edits
        ? record.edits
            .map((edit) =>
              typeof edit === "string" ? edit : lines.slice(...edit).join(""),
            )
            .join("")
        : base;
      if (record.numbered) {
        let number = 0;
        text = text.replace(
          /<span data-source-line><span class="lineNum">@LINE@<\/span>/g,
          () =>
            `<span id="L${++number}"><span class="lineNum">${String(number).padStart(8)}</span>`,
        );
      }
      return text;
    }
    const file = query.get("file") || "index.html";
    const entry = archive.files[file];
    if (!entry || entry[0] !== "text/html")
      throw new Error("Source page not available in this report");
    const page = new DOMParser().parseFromString(
      await content(entry),
      "text/html",
    );
    page.querySelectorAll("script,base").forEach((element) => element.remove());
    const original = new URL(file, "https://coverage.invalid/");
    for (const element of page.querySelectorAll("[src],[href]")) {
      const attribute = element.hasAttribute("src") ? "src" : "href";
      const url = new URL(element.getAttribute(attribute), original);
      const path = decodeURIComponent(url.pathname.slice(1));
      const resource = archive.files[path];
      if (url.origin === original.origin && resource) {
        if (resource[0] === "text/html") {
          const target = new URL("view.html", location.href);
          target.search = new URLSearchParams({ report, file: path });
          target.hash = url.hash;
          element.href = target;
          element.target = "_top";
        } else {
          const text = await content(resource);
          const bytes = resource[0].startsWith("image/")
            ? Uint8Array.from(atob(text), (character) =>
                character.charCodeAt(0),
              )
            : text;
          element.setAttribute(
            attribute,
            URL.createObjectURL(new Blob([bytes], { type: resource[0] })),
          );
        }
      } else if (url.origin === original.origin) {
        // Navigation out of the archive goes to the retained summary, never a missing source tree.
        if (attribute === "href") {
          element.href = new URL(report + "/", location.href);
          element.target = "_top";
        } else element.removeAttribute(attribute);
      } else if (!["https:", "http:"].includes(url.protocol)) {
        element.removeAttribute(attribute);
      } else if (attribute === "href") element.target = "_top";
    }
    const frame = document.querySelector("iframe");
    frame.addEventListener(
      "load",
      () => {
        const anchor = decodeURIComponent(location.hash.slice(1));
        if (anchor)
          frame.contentDocument.getElementById(anchor)?.scrollIntoView();
      },
      { once: true },
    );
    frame.srcdoc = page.documentElement.outerHTML;
    frame.hidden = false;
    status.hidden = true;
  } catch (error) {
    status.textContent = "Coverage details unavailable: " + error.message;
  }
})();
