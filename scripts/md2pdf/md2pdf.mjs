#!/usr/bin/env node

import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';

import puppeteer from 'puppeteer';
import { marked } from 'marked';
import hljs from 'highlight.js';
import yargs from 'yargs/yargs';
import { hideBin } from 'yargs/helpers';

function defaultCss() {
  return `
  :root {
    --fg: #111;
    --muted: #555;
    --bg: #fff;
    --code-bg: #f6f8fa;
    --border: #e5e7eb;
    --link: #0b5fff;
  }

  html, body { background: var(--bg); color: var(--fg); }
  body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, "Noto Sans", "Liberation Sans", sans-serif;
    font-size: 13.5px;
    line-height: 1.55;
    margin: 0;
    padding: 32px 44px;
  }

  a { color: var(--link); text-decoration: none; }
  a:hover { text-decoration: underline; }

  h1, h2, h3, h4 { line-height: 1.25; margin-top: 1.25em; }
  h1 { font-size: 28px; margin-top: 0; }
  h2 { font-size: 20px; border-bottom: 1px solid var(--border); padding-bottom: 6px; }
  h3 { font-size: 16px; }
  h4 { font-size: 14px; }

  p { margin: 0.6em 0; }

  code {
    font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace;
    font-size: 12.5px;
    background: var(--code-bg);
    padding: 0.12em 0.35em;
    border-radius: 4px;
  }

  pre {
    background: var(--code-bg);
    padding: 12px 14px;
    border-radius: 8px;
    overflow: hidden;
    border: 1px solid var(--border);
  }
  pre code { background: transparent; padding: 0; }

  blockquote {
    margin: 1em 0;
    padding: 0.6em 1em;
    border-left: 4px solid var(--border);
    color: var(--muted);
    background: #fafafa;
  }

  table {
    border-collapse: collapse;
    width: 100%;
    margin: 1em 0;
  }
  th, td {
    border: 1px solid var(--border);
    padding: 8px 10px;
    vertical-align: top;
  }
  th { background: #fafafa; }

  hr { border: none; border-top: 1px solid var(--border); margin: 18px 0; }

  ul, ol { padding-left: 1.4em; }
  li { margin: 0.25em 0; }

  /* Better printing */
  @page { margin: 18mm 14mm; }
  `;
}

function escapeHtml(s) {
  return s
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

async function ensureDir(p) {
  await fs.mkdir(p, { recursive: true });
}

function isoStamp() {
  // YYYY-MM-DD HH:MM (local)
  const d = new Date();
  const pad = (n) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

marked.setOptions({
  gfm: true,
  breaks: false,
  headerIds: true,
  mangle: false,
  highlight: (code, lang) => {
    try {
      if (lang && hljs.getLanguage(lang)) {
        return hljs.highlight(code, { language: lang }).value;
      }
      return hljs.highlightAuto(code).value;
    } catch {
      return escapeHtml(code);
    }
  }
});

const argv = yargs(hideBin(process.argv))
  .scriptName('animus-md2pdf')
  .usage('$0 <input.md...> [options]')
  .option('out', {
    type: 'string',
    describe: 'Output PDF path (single input only)'
  })
  .option('out-dir', {
    type: 'string',
    describe: 'Output directory (for multiple inputs or default output)',
    default: 'exports/pdfs'
  })
  .option('format', {
    type: 'string',
    describe: 'PDF page format: A4 or Letter',
    default: 'A4'
  })
  .option('css', {
    type: 'string',
    describe: 'Path to a CSS file to append after the default styling'
  })
  .option('no-footer', {
    type: 'boolean',
    describe: 'Disable footer (page numbers / filename)',
    default: false
  })
  .option('keep-html', {
    type: 'boolean',
    describe: 'Keep intermediate HTML files (debugging)',
    default: false
  })
  .demandCommand(1)
  .help()
  .parse();

const inputs = argv._.map(String);

if (argv.out && inputs.length !== 1) {
  console.error('Error: --out can only be used with a single input file.');
  process.exit(2);
}

const repoRoot = process.cwd();

async function buildHtml({ mdPath, cssExtra }) {
  const mdAbs = path.resolve(repoRoot, mdPath);
  const mdDir = path.dirname(mdAbs);
  const md = await fs.readFile(mdAbs, 'utf8');

  const bodyHtml = marked.parse(md);

  // Base href so relative links/images resolve from the markdown directory.
  const baseHref = `file://${mdDir.replaceAll('\\', '/')}/`;

  const html = `<!doctype html>
<html>
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <base href="${baseHref}" />
    <title>${escapeHtml(path.basename(mdAbs))}</title>
    <style>${defaultCss()}</style>
    ${cssExtra ? `<style>${cssExtra}</style>` : ''}
  </head>
  <body>
    ${bodyHtml}
  </body>
</html>`;

  return { html, mdAbs };
}

async function main() {
  const outDir = path.resolve(repoRoot, argv['out-dir']);
  await ensureDir(outDir);

  let cssExtra = '';
  if (argv.css) {
    cssExtra = await fs.readFile(path.resolve(repoRoot, argv.css), 'utf8');
  }

  const tmpRoot = await fs.mkdtemp(path.join(os.tmpdir(), 'animus-md2pdf-'));
  const htmlPaths = [];

  for (const input of inputs) {
    const { html, mdAbs } = await buildHtml({ mdPath: input, cssExtra });
    const htmlName = path.basename(mdAbs).replace(/\.md$/i, '') + '.html';
    const htmlPath = path.join(tmpRoot, htmlName);
    await fs.writeFile(htmlPath, html, 'utf8');
    htmlPaths.push({ input, mdAbs, htmlPath });
  }

  const browser = await puppeteer.launch({
    headless: 'new',
    args: [
      '--no-sandbox',
      '--disable-setuid-sandbox',
      '--disable-dev-shm-usage',
      '--no-zygote',
      '--disable-gpu',
      '--disable-crash-reporter',
      '--disable-features=Crashpad'
    ]
  });

  try {
    const page = await browser.newPage();

    for (const { input, mdAbs, htmlPath } of htmlPaths) {
      const pdfPath = argv.out
        ? path.resolve(repoRoot, argv.out)
        : path.join(outDir, path.basename(mdAbs).replace(/\.md$/i, '') + '.pdf');

      await ensureDir(path.dirname(pdfPath));

      await page.goto(`file://${htmlPath.replaceAll('\\', '/')}`, {
        waitUntil: 'networkidle0'
      });

      const footer = argv['no-footer']
        ? { displayHeaderFooter: false }
        : {
            displayHeaderFooter: true,
            headerTemplate: '<div></div>',
            footerTemplate: `
              <div style="width: 100%; font-size: 9px; color: #666; padding: 0 12mm;">
                <div style="display: flex; justify-content: space-between; width: 100%;">
                  <span>${escapeHtml(path.basename(mdAbs))} • ${escapeHtml(isoStamp())}</span>
                  <span><span class="pageNumber"></span> / <span class="totalPages"></span></span>
                </div>
              </div>
            `
          };

      await page.pdf({
        path: pdfPath,
        printBackground: true,
        format: argv.format,
        preferCSSPageSize: false,
        margin: { top: '18mm', right: '14mm', bottom: argv['no-footer'] ? '18mm' : '22mm', left: '14mm' },
        ...footer
      });

      console.log(`${input} -> ${path.relative(repoRoot, pdfPath)}`);
    }
  } finally {
    await browser.close();
  }

  if (!argv['keep-html']) {
    // Best-effort cleanup (ignore errors)
    try {
      const files = await fs.readdir(tmpRoot);
      await Promise.all(files.map((f) => fs.unlink(path.join(tmpRoot, f)).catch(() => {})));
      await fs.rmdir(tmpRoot).catch(() => {});
    } catch {
      // ignore
    }
  } else {
    console.error(`Kept intermediate HTML in: ${tmpRoot}`);
  }
}

main().catch((err) => {
  console.error(err?.stack || String(err));
  process.exit(1);
});
