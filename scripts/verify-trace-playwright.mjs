#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';

import { chromium } from 'playwright';

const DEFAULT_EVENT_SUBSTRINGS = ['withRecording-demo'];
const DEFAULT_TRACE_DIR = path.join(process.cwd(), 'output', 'playwright');

function printUsage() {
  console.log(`Usage:
  node scripts/verify-trace-playwright.mjs [trace-file] [--event <substring>] [--trace <trace-file>] [--min-dur-ms <ms>] [--max-dur-ms <ms>] [--headed] [--keep-open]

Examples:
  node scripts/verify-trace-playwright.mjs
  node scripts/verify-trace-playwright.mjs output/playwright/trace.perfetto-trace
  node scripts/verify-trace-playwright.mjs --event withRecording-demo --event manual-synthetic-work
  node scripts/verify-trace-playwright.mjs --event busy-loop-1s --min-dur-ms 900 --max-dur-ms 1500
  node scripts/verify-trace-playwright.mjs --headed --keep-open
`);
}

function findLatestTraceFile(dir) {
  if (!fs.existsSync(dir)) {
    return null;
  }

  const candidates = fs
    .readdirSync(dir)
    .filter((name) => name.endsWith('.perfetto-trace'))
    .map((name) => {
      const absolutePath = path.join(dir, name);
      return {
        absolutePath,
        mtimeMs: fs.statSync(absolutePath).mtimeMs,
      };
    })
    .sort((a, b) => b.mtimeMs - a.mtimeMs);

  return candidates.length > 0 ? candidates[0].absolutePath : null;
}

function parseArgs(argv) {
  const options = {
    tracePath: null,
    eventSubstrings: [],
    minDurMs: null,
    maxDurMs: null,
    headed: false,
    keepOpen: false,
  };

  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];

    if (arg === '--') {
      continue;
    }

    if (arg === '--help' || arg === '-h') {
      printUsage();
      process.exit(0);
    }

    if (arg === '--trace') {
      const value = argv[i + 1];
      if (!value) {
        throw new Error('Missing value for --trace.');
      }
      options.tracePath = value;
      i += 1;
      continue;
    }

    if (arg === '--event') {
      const value = argv[i + 1];
      if (!value) {
        throw new Error('Missing value for --event.');
      }
      options.eventSubstrings.push(value);
      i += 1;
      continue;
    }

    if (arg === '--min-dur-ms') {
      const value = argv[i + 1];
      if (!value) {
        throw new Error('Missing value for --min-dur-ms.');
      }

      const minDurMs = Number(value);
      if (!Number.isFinite(minDurMs) || minDurMs < 0) {
        throw new Error(`Invalid --min-dur-ms value: "${value}"`);
      }

      options.minDurMs = minDurMs;
      i += 1;
      continue;
    }

    if (arg === '--max-dur-ms') {
      const value = argv[i + 1];
      if (!value) {
        throw new Error('Missing value for --max-dur-ms.');
      }

      const maxDurMs = Number(value);
      if (!Number.isFinite(maxDurMs) || maxDurMs < 0) {
        throw new Error(`Invalid --max-dur-ms value: "${value}"`);
      }

      options.maxDurMs = maxDurMs;
      i += 1;
      continue;
    }

    if (arg === '--headed') {
      options.headed = true;
      continue;
    }

    if (arg === '--keep-open') {
      options.keepOpen = true;
      continue;
    }

    if (arg.startsWith('-')) {
      throw new Error(`Unknown option: ${arg}`);
    }

    if (options.tracePath) {
      throw new Error(
        `Trace file already provided (${options.tracePath}). Unexpected argument: ${arg}`
      );
    }

    options.tracePath = arg;
  }

  if (!options.tracePath) {
    options.tracePath = findLatestTraceFile(DEFAULT_TRACE_DIR);
  }

  if (!options.tracePath) {
    throw new Error(
      `No trace file provided and none found in ${DEFAULT_TRACE_DIR}. ` +
        'Pull a trace first or pass an explicit path.'
    );
  }

  if (options.eventSubstrings.length === 0) {
    options.eventSubstrings = DEFAULT_EVENT_SUBSTRINGS;
  }

  if (
    options.minDurMs !== null &&
    options.maxDurMs !== null &&
    options.minDurMs > options.maxDurMs
  ) {
    throw new Error('--min-dur-ms cannot be greater than --max-dur-ms.');
  }

  options.tracePath = path.resolve(options.tracePath);
  return options;
}

function sqlStringLiteral(value) {
  return `'${value.replace(/'/g, "''")}'`;
}

async function dismissCookieBannerIfVisible(page) {
  const okButton = page.getByRole('button', { name: /^OK$/i });
  if (await okButton.count()) {
    try {
      if (await okButton.first().isVisible({ timeout: 1_000 })) {
        await okButton.first().click();
      }
    } catch {
      // Ignore best-effort cookie banner dismissal.
    }
  }
}

async function openTraceInPerfetto(page, tracePath) {
  await page.goto('https://ui.perfetto.dev', { waitUntil: 'domcontentloaded' });
  await dismissCookieBannerIfVisible(page);

  const [fileChooser] = await Promise.all([
    page.waitForEvent('filechooser'),
    page.getByRole('link', { name: /open trace file/i }).click(),
  ]);

  await fileChooser.setFiles(tracePath);
  await page.waitForURL(/local_cache_key=/, { timeout: 120_000 });

  const fileName = path.basename(tracePath);
  await page.waitForFunction(
    ({ expectedFileName }) => document.title.includes(expectedFileName),
    { expectedFileName: fileName },
    { timeout: 120_000 }
  );
}

async function runNumericQuery(page, sql) {
  const queryEditor = page.getByRole('textbox').nth(1);
  const runQueryButton = page.getByRole('button', { name: /run query/i });
  const queryHistoryText = page.getByText(/Query history \(\d+ queries\)/i);

  const historyBefore = await queryHistoryText.first().textContent().catch(() => null);
  const historyBeforeCount =
    historyBefore?.match(/Query history \((\d+) queries\)/i)?.[1] ?? null;

  await queryEditor.fill(sql);
  await runQueryButton.click();

  if (historyBeforeCount !== null) {
    const nextCount = Number(historyBeforeCount) + 1;
    await page
      .getByText(new RegExp(`Query history \\(${nextCount} queries\\)`, 'i'))
      .waitFor({ state: 'visible', timeout: 30_000 });
  }

  await page
    .getByText(/Returned \d+ rows in .* ms/i)
    .first()
    .waitFor({ state: 'visible', timeout: 30_000 });

  const firstDataCell = page.getByRole('cell').first();
  await firstDataCell.waitFor({ state: 'visible', timeout: 30_000 });

  const rawValue = (await firstDataCell.innerText()).trim();
  const numericValue = Number(rawValue);
  if (!Number.isFinite(numericValue)) {
    throw new Error(`Expected numeric query result, got: "${rawValue}"`);
  }

  return numericValue;
}

async function verifyTraceSections(page, options) {
  const { eventSubstrings, minDurMs, maxDurMs } = options;

  await page.getByRole('link', { name: /query \(sql\)/i }).click();
  await page.waitForURL(/#!\/query/, { timeout: 30_000 });

  const queryEditor = page.getByRole('textbox').nth(1);
  await queryEditor.waitFor({ state: 'visible', timeout: 30_000 });

  const results = [];

  for (const eventSubstring of eventSubstrings) {
    const sql = `
      SELECT COUNT(*) AS matching_sections
      FROM slice
      WHERE name LIKE ${sqlStringLiteral(`%${eventSubstring}%`)}
        AND dur > 0;
    `;

    const matchingSections = await runNumericQuery(page, sql);

    if (matchingSections < 1) {
      throw new Error(
        `No completed begin/end section found for substring "${eventSubstring}".`
      );
    }

    let maxDurationMs = null;
    if (minDurMs !== null || maxDurMs !== null) {
      const maxDurationSql = `
        SELECT COALESCE(MAX(CAST(dur AS REAL) / 1000000.0), 0) AS max_duration_ms
        FROM slice
        WHERE name LIKE ${sqlStringLiteral(`%${eventSubstring}%`)}
          AND dur > 0;
      `;

      maxDurationMs = await runNumericQuery(page, maxDurationSql);

      if (minDurMs !== null && maxDurationMs < minDurMs) {
        throw new Error(
          `Section "${eventSubstring}" max duration (${maxDurationMs.toFixed(2)} ms) is below --min-dur-ms (${minDurMs}).`
        );
      }

      if (maxDurMs !== null && maxDurationMs > maxDurMs) {
        throw new Error(
          `Section "${eventSubstring}" max duration (${maxDurationMs.toFixed(2)} ms) is above --max-dur-ms (${maxDurMs}).`
        );
      }
    }

    results.push({ eventSubstring, matchingSections, maxDurationMs });
  }

  return results;
}

async function keepBrowserOpenUntilExit(browser) {
  console.log(
    'Keeping browser open for inspection. Press Ctrl+C or close the browser to exit.'
  );

  await new Promise((resolve) => {
    let isDone = false;

    const finish = () => {
      if (isDone) {
        return;
      }
      isDone = true;
      process.off('SIGINT', onSignal);
      process.off('SIGTERM', onSignal);
      browser.off('disconnected', onDisconnected);
      resolve();
    };

    const onDisconnected = () => {
      finish();
    };

    const onSignal = () => {
      void browser
        .close()
        .catch(() => {
          // Best-effort close during shutdown.
        })
        .finally(finish);
    };

    process.on('SIGINT', onSignal);
    process.on('SIGTERM', onSignal);
    browser.on('disconnected', onDisconnected);
  });
}

async function main() {
  const options = parseArgs(process.argv.slice(2));

  if (!fs.existsSync(options.tracePath)) {
    throw new Error(`Trace file does not exist: ${options.tracePath}`);
  }

  console.log(`Trace file: ${options.tracePath}`);
  console.log(`Expected section substrings: ${options.eventSubstrings.join(', ')}`);
  if (options.minDurMs !== null || options.maxDurMs !== null) {
    console.log(
      `Duration bounds: min=${options.minDurMs ?? 'none'} ms, max=${options.maxDurMs ?? 'none'} ms`
    );
  }
  if (options.keepOpen && !options.headed) {
    console.log('Keeping a headless browser open. Pass --headed to inspect the UI.');
  }

  const browser = await chromium.launch({
    headless: !options.headed,
  });

  try {
    const page = await browser.newPage();
    page.setDefaultTimeout(30_000);

    await openTraceInPerfetto(page, options.tracePath);
    const results = await verifyTraceSections(page, options);

    for (const result of results) {
      const durationText =
        result.maxDurationMs === null
          ? ''
          : `; max duration ${result.maxDurationMs.toFixed(2)} ms`;
      console.log(
        `Verified section "${result.eventSubstring}" with ${result.matchingSections} matching completed slice(s)${durationText}.`
      );
    }

    console.log('Perfetto trace verification passed.');
  } finally {
    if (options.keepOpen) {
      await keepBrowserOpenUntilExit(browser);
    } else {
      await browser.close();
    }
  }
}

main().catch((error) => {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
});
