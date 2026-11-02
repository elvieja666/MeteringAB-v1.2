const { chromium } = require('playwright');
(async () => {
  const b = await chromium.launch();
  const p = await b.newPage();
  await p.goto('file://' + process.argv[2], { waitUntil: 'networkidle' });
  await p.evaluate(() => document.fonts.ready);
  await p.pdf({ path: process.argv[3], format: 'A4', printBackground: true, preferCSSPageSize: true,
                displayHeaderFooter: true, headerTemplate: '<span></span>',
                footerTemplate: '<div style="width:100%;font-size:8px;color:#8a8f98;text-align:center;font-family:sans-serif">UNISON · Manual · <span class="pageNumber"></span></div>' });
  await b.close();
})();
