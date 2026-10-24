// Memory errors and leaks under valgrind. Linux build only.
//
// isère exits without freeing its long-lived state, so a single run's leak report
// mixes start-up allocations with per-request leaks. Instead, run twice with different
// request counts and compare: anything that grows with the request count is a leak,
// whether valgrind classifies it as lost or still reachable.
//
//   ISERE_VALGRIND=0  skip this file

import { describe, it, before } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';

import { isRemote, startServer } from './lib/target.mjs';
import { request } from './lib/http.mjs';

const SMALL = 20;
const LARGE = 120;
// allocator bookkeeping can shift a few bytes between runs; a real leak is at least one block per request
const MAX_GROWTH_PER_REQUEST = 8;

function hasValgrind() {
  try {
    execFileSync('valgrind', ['--version'], { stdio: 'ignore' });
    return true;
  } catch {
    return false;
  }
}

const skip = isRemote ? 'needs the Linux build'
  : process.env.ISERE_VALGRIND === '0' ? 'ISERE_VALGRIND=0'
  : !hasValgrind() ? 'valgrind is not installed'
  : false;

function bytes(report, kind) {
  const m = report.match(new RegExp(`${kind}: ([\\d,]+) bytes`));
  return m ? Number(m[1].replace(/,/g, '')) : 0;
}

// Runs `requests` rounds of mixed traffic under valgrind and returns the parsed report.
async function run(requests) {
  const logFile = path.join(mkdtempSync(path.join(tmpdir(), 'isere-vg-')), 'valgrind.log');
  const server = await startServer({
    valgrind: ['--leak-check=full', '--show-leak-kinds=all', '--errors-for-leak-kinds=none', `--log-file=${logFile}`],
  });
  for (let i = 0; i < requests; i++) {
    // the JS handler, the built-in endpoint and the 404 path each exercise different cleanup
    const path = ['/', '/__version', '/does-not-exist'][i % 3];
    await request(server.url, path, { timeoutMs: 10_000 });
  }
  const result = await server.stop();
  assert.ok(!result.hung, 'server did not shut down on SIGINT under valgrind');

  const report = readFileSync(logFile, 'utf8');
  const errors = Number(report.match(/ERROR SUMMARY: (\d+) errors/)?.[1] ?? NaN);
  return {
    report,
    logFile,
    errors,
    lost: bytes(report, 'definitely lost') + bytes(report, 'indirectly lost'),
    retained: bytes(report, 'possibly lost') + bytes(report, 'still reachable'),
  };
}

describe('memory (valgrind)', { skip }, () => {
  let small;
  let large;
  before(async () => {
    small = await run(SMALL);
    large = await run(LARGE);
  });

  it('makes no invalid reads, writes or frees', () => {
    assert.equal(large.errors, 0, `valgrind reported ${large.errors} errors, see ${large.logFile}`);
  });

  it('loses no memory per request', () => {
    assert.equal(large.lost - small.lost, 0,
      `definitely/indirectly lost grew by ${large.lost - small.lost} bytes over ${LARGE - SMALL} requests, see ${large.logFile}`);
  });

  it('retains no memory per request', () => {
    const perRequest = (large.retained - small.retained) / (LARGE - SMALL);
    assert.ok(perRequest <= MAX_GROWTH_PER_REQUEST,
      `retained memory grew by ${perRequest.toFixed(1)} bytes per request, see ${large.logFile}`);
  });
});
