// Memory errors and leaks under valgrind (criterion 8). Linux build only.
//
// Each mode runs twice with different request counts. A single report can't tell
// start-up allocations from per-request leaks, but anything that grows with the
// request count is a leak, whether valgrind calls it lost or still reachable.
// isère frees everything at shutdown, so a clean run retains next to nothing.
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
import { sign } from './lib/jwt.mjs';

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

// the JS handler, built-ins, 404, static files, JWT verification with its loopback
// fetch, failures and requests that leave timers or promises behind
function mix(i) {
  const token = sign();
  return [
    ['/'],
    ['/__version'],
    ['/does-not-exist'],
    ['/.well-known/jwks.json'],
    ['/verify', { headers: { authorization: `Bearer ${token}` } }],
    ['/verify', { headers: { authorization: 'Bearer bad.token.here' } }],
    ['/__test/throw'],
    ['/__test/timers-pending'],
    ['/__test/promise-pending'],
    ['/__test/echo', { method: 'POST', body: 'x'.repeat(100 + i) }],
    ['/__calibrate/alloc'],
    ['/__test/crypto'],
  ][i % 12];
}

async function run(requests, mode) {
  const logFile = path.join(mkdtempSync(path.join(tmpdir(), 'isere-vg-')), 'valgrind.log');
  const server = await startServer({
    valgrind: ['--leak-check=full', '--show-leak-kinds=all', '--errors-for-leak-kinds=none', `--log-file=${logFile}`],
    env: { ISERE_JS_MODE: mode, ISERE_HANDLER_TIMEOUT_MS: '30000', ISERE_NDJSON: '0' },
  });
  for (let i = 0; i < requests; i++) {
    const [p, opts] = mix(i);
    await request(server.url, p, { timeoutMs: 30_000, ...opts });
  }
  const result = await server.stop();
  const tail = (text) => text.split('\n').slice(-15).join('\n');
  assert.ok(!result.hung, `server did not shut down on SIGINT under valgrind\n--- stderr ---\n${tail(result.stderr)}\n--- valgrind ---\n${tail(readFileSync(logFile, 'utf8'))}`);
  assert.equal(result.code, 0, `exit code ${result.code}\n${result.stderr.slice(-2000)}`);

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

for (const mode of ['warm', 'cold']) {
  describe(`memory under valgrind (${mode} engine)`, { skip }, () => {
    let small;
    let large;
    before(async () => {
      small = await run(SMALL, mode);
      large = await run(LARGE, mode);
    });

    it('makes no invalid reads, writes or frees', () => {
      assert.equal(small.errors, 0, `valgrind reported ${small.errors} errors, see ${small.logFile}`);
      assert.equal(large.errors, 0, `valgrind reported ${large.errors} errors, see ${large.logFile}`);
    });

    it('loses no memory per request', () => {
      assert.equal(large.lost - small.lost, 0,
        `definitely/indirectly lost grew by ${large.lost - small.lost} bytes over ${LARGE - SMALL} requests, see ${large.logFile}`);
    });

    it(`retains no more than ${MAX_GROWTH_PER_REQUEST} bytes per request`, () => {
      const perRequest = (large.retained - small.retained) / (LARGE - SMALL);
      assert.ok(perRequest <= MAX_GROWTH_PER_REQUEST,
        `retained memory grew by ${perRequest.toFixed(1)} bytes per request, see ${large.logFile}`);
    });
  });
}
