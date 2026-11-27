// Chup logo-icon.html (bieu tuong logo EVCarHUD, nen trong suot) thanh logo-icon.png 192x192. Can Playwright + Chromium.
// Chay: node tools/logo/render_icon.js
const { chromium } = require('playwright');
const path = require('path');
(async () => {
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 192, height: 192 } });
  await p.goto('file://' + path.join(__dirname, 'logo-icon.html'));
  await p.screenshot({ path: path.join(__dirname, 'logo-icon.png'), omitBackground: true, clip: { x: 0, y: 0, width: 192, height: 192 } });
  await b.close();
})();
