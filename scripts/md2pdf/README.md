# Animus md2pdf

Local Markdown → PDF conversion for Animus docs.

## Why this exists

- We’ll be generating stakeholder-friendly PDFs a lot.
- This converter runs **locally** (no online Markdown rendering), so confidential docs don’t get sent to a third party.

## Install (one-time)

From the repo root:

```sh
npm --prefix scripts/md2pdf install
```

Note: this installs Puppeteer, which downloads a compatible headless Chromium.

## Usage

Convert one file (output defaults to `exports/pdfs/<name>.pdf`):

```sh
npm --prefix scripts/md2pdf exec animus-md2pdf -- CONCEPT.md
```

Convert multiple files into a chosen output directory:

```sh
npm --prefix scripts/md2pdf exec animus-md2pdf -- CONCEPT.md MARKET_RESEARCH_*.md --out-dir exports/pdfs
```

Write a specific output path (single input only):

```sh
npm --prefix scripts/md2pdf exec animus-md2pdf -- CONCEPT.md --out exports/pdfs/CONCEPT.pdf
```

## Notes

- Relative links/images are resolved relative to the input Markdown file.
- By default, PDFs include a small footer with filename + timestamp + page numbers.
