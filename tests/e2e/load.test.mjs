import { describe, it, before, after } from 'node:test';
import assert from 'node:assert/strict';

import { startServer } from './lib/target.mjs';
import { request } from './lib/http.mjs';

// The Pico 2 accepts 4 connections at a time, so load stays within that.
const LEVELS = [1, 2, 4];
const BURST_MS = Number(process.env.ISERE_BURST_MS || 2000);

// Keeps `concurrency` requests in flight for `durationMs`; returns counts per outcome.
async function burst(url, concurrency, durationMs) {
  const deadline = Date.now() + durationMs;
  const counts = { ok: 0, failed: 0, errors: new Set() };
  async function worker() {
    while (Date.now() < deadline) {
      try {
        const res = await request(url, '/', { timeoutMs: 5000 });
        if (res.status === 200) counts.ok++;
        else { counts.failed++; counts.errors.add(`status ${res.status}`); }
      } catch (err) {
        counts.failed++;
        counts.errors.add(err.code || err.message);
      }
    }
  }
  await Promise.all(Array.from({ length: concurrency }, worker));
  return counts;
}

describe('load', () => {
  let server;
  before(async () => { server = await startServer(); });
  after(async () => { await server.stop(); });

  for (const concurrency of LEVELS) {
    it(`serves ${concurrency} concurrent clients without errors`, async () => {
      const counts = await burst(server.url, concurrency, BURST_MS);
      assert.equal(counts.failed, 0, `failures: ${[...counts.errors].join(', ')}`);
      assert.ok(counts.ok > 0, 'no request succeeded');
    });
  }

  it('keeps accepting connections after the load above', async () => {
    const res = await request(server.url, '/__version', { timeoutMs: 5000 });
    assert.equal(res.status, 200);
  });
});
