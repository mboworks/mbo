// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
const packedPage = document.currentScript.dataset.packedPage;
(async () => {
  try {
    const response = await fetch(packedPage);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const markup = await new Response(
      response.body.pipeThrough(new DecompressionStream("gzip")),
    ).text();
    // This is the same generated, trusted HTML previously served directly at this URL.
    // Retain the URL so all relative assets, navigation and fragments keep their meaning.
    document.open();
    document.write(markup);
    document.close();
  } catch (error) {
    document.getElementById("loading").textContent =
      "Report unavailable: " + error.message;
  }
})();
