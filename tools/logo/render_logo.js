// Chup logo-khoi-dong.html thanh logo-khoi-dong.png (240x136). Can Playwright + Chromium, file font Inter 800 (woff2).
// Chay: node tools/logo/render_logo.js <duong-dan-inter-latin-800-normal.woff2>
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const font = 'file://' + path.resolve(process.argv[2]);
  const html = fs.readFileSync(path.join(__dirname, 'logo-khoi-dong.html'), 'utf8').replace('INTER800', font);
  const tmp = path.join(require('os').tmpdir(), 'logo-khoi-dong.html');
  fs.writeFileSync(tmp, html);
  const b = await chromium.launch();
  const p = await b.newPage({ deviceScaleFactor: 1 });
  await p.goto('file://' + tmp);
  await p.evaluate(() => document.fonts.ready);
  await (await p.$('#b')).screenshot({ path: path.join(__dirname, 'logo-khoi-dong.png') });
  await b.close();
})();
