// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { gunzipSync } from "node:zlib";
import { chromium } from "playwright";

test("retained coverage: packed summaries, source navigation, old URLs and expiry", async () => {
  const root = mkdtempSync(join(tmpdir(), "mbo-coverage-web-"));
  const options = { headless: true };
  if (process.env.CHROME_PATH) options.executablePath = process.env.CHROME_PATH;
  let server, browser;
  try {
    execFileSync(process.env.PYTHON || "python3", [
      fileURLToPath(new URL("fixture.py", import.meta.url)),
      root,
    ]);
    server = createServer((request, response) => {
      const path = new URL(request.url, "http://localhost").pathname;
      try {
        assert.ok(path.startsWith("/mbo/"));
        const relative = decodeURIComponent(path.slice("/mbo/".length));
        const file = join(
          root,
          relative.endsWith("/") ? relative + "index.html" : relative,
        );
        response.setHeader(
          "Content-Type",
          path.endsWith(".js")
            ? "text/javascript"
            : path.endsWith(".gz")
              ? "application/octet-stream"
              : path.endsWith(".json")
                ? "application/json"
                : "text/html",
        );
        response.end(readFileSync(file));
      } catch {
        // GitHub Pages serves the site's 404 document at the original deep URL.
        response.writeHead(404, { "Content-Type": "text/html" });
        response.end(readFileSync(join(root, "404.html")));
      }
    });
    await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
    const base = `http://127.0.0.1:${server.address().port}/mbo`;
    browser = await chromium.launch(options);
    const page = await browser.newPage();
    page.setDefaultTimeout(10000);
    const errors = [];
    page.on("pageerror", (error) => errors.push(error.message));
    await page.goto(base + "/coverage/runs/2/1/");
    await page.waitForSelector("table.coverageTable");
    const raw = await page.request.get(
      base + "/coverage/runs/2/1/coverage-summary.json.gz",
    );
    assert.equal(raw.ok(), true);
    const data = JSON.parse(gunzipSync(await raw.body()));
    assert.deepEqual(data.measurements.overall.lines, {
      covered: 9,
      total: 10,
      percent: 90,
    });
    assert.equal(data.minimums.overall.lines, 80);
    await page
      .getByText("Browse detailed LCOV source coverage", { exact: true })
      .click();
    await page
      .frameLocator("iframe")
      .getByText("Source", { exact: true })
      .click();
    await page.waitForURL(/file=file.cc.gcov.html#L100/);
    const frame = page.frameLocator("iframe");
    assert.equal(
      await frame.locator("#L100").textContent(),
      "     100return 100;",
    );
    assert.equal(
      await frame
        .locator("body")
        .evaluate((element) => getComputedStyle(element).color),
      "rgb(1, 2, 3)",
    );
    await page.waitForFunction(
      () => document.querySelector("iframe").contentWindow.scrollY > 0,
    );
    await frame.getByText("Source index", { exact: true }).click();
    await page
      .frameLocator("iframe")
      .getByText("Source", { exact: true })
      .waitFor();
    // Existing direct links retain the report identity and source-line fragment.
    await page.goto(base + "/coverage/pr/12/lcov/file.cc.gcov.html#L100");
    await page.waitForURL(
      /view.html\?report=pr%2F12&file=file.cc.gcov.html#L100/,
    );
    await page.frameLocator("iframe").locator("#L100").waitFor();
    await page.goto(base + "/coverage/runs/1/1/lcov/file.cc.gcov.html#L100");
    await page.waitForFunction(() =>
      document.getElementById("status")?.textContent.includes("seven days"),
    );
    assert.equal(await page.locator("iframe").isVisible(), false);
    await page.locator("#summary").click();
    await page.waitForSelector("table.coverageTable");
    assert.deepEqual(errors, []);
    const noScript = await browser.newPage({ javaScriptEnabled: false });
    await noScript.goto(base + "/coverage/runs/1/1/");
    assert.equal(
      await noScript.locator("noscript a").getAttribute("href"),
      "coverage-summary.json.gz",
    );
  } finally {
    if (browser) await browser.close();
    if (server) await new Promise((resolve) => server.close(resolve));
    rmSync(root, { recursive: true, force: true });
  }
});
