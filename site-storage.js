// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
window.MboSiteStorage = {
  async gzip(url) {
    const response = await fetch(url);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const text = await new Response(
      response.body.pipeThrough(new DecompressionStream("gzip")),
    ).text();
    return JSON.parse(text);
  },
};
