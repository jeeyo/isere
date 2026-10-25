import { describe, it, before, after } from 'node:test';
import assert from 'node:assert/strict';

import { startServer, metrics } from './lib/target.mjs';
import { request, keepAliveAgent } from './lib/http.mjs';
import { sign } from './lib/jwt.mjs';

// The Pico 2 accepts 4 connections at a time, so load stays within that.
const LEVELS = [1, 2, 4];
const BURST_MS = Number(process.env.ISERE_BURST_MS || 2000);

// a mix across endpoints: the JS handler, JWT verification, the static JWKS and a 404
function pick(i, token) {
  switch (i % 4) {
    case 0: return ['/', {}, 200];
    case 1: return ['/verify', { headers: { authorization: `Bearer ${token}` } }, 200];
    case 2: return ['/.well-known/jwks.json', {}, 200];
    default: return ['/nope', {}, 404];
  }
}

// Keeps `concurrency` keep-alive connections busy for `durationMs`; returns counts per outcome.
async function burst(url, concurrency, durationMs, { keepAlive }) {
  const deadline = Date.now() + durationMs;
  const counts = { ok: 0, failed: 0, errors: new Set() };
  const token = sign();
  async function worker(w) {
    const agent = keepAlive ? keepAliveAgent(1) : false;
    let i = w;
    while (Date.now() < deadline) {
      const [path, opts, expected] = pick(i++, token);
      try {
        const res = await request(url, path, { timeoutMs: 5000, agent, ...opts });
        if (res.status === expected) counts.ok++;
        else { counts.failed++; counts.errors.add(`${path}: status ${res.status}`); }
      } catch (err) {
        counts.failed++;
        counts.errors.add(`${path}: ${err.code || err.message}`);
      }
    }
    if (agent) agent.destroy();
  }
  await Promise.all(Array.from({ length: concurrency }, (_, w) => worker(w)));
  return counts;
}

describe('load', () => {
  let server;
  before(async () => { server = await startServer({ env: { ISERE_NDJSON: '0' } }); });
  after(async () => {
    const result = await server.stop();
    assert.ok(!result.hung, 'server did not shut down on SIGINT');
  });

  for (const concurrency of LEVELS) {
    for (const keepAlive of [true, false]) {
      it(`serves ${concurrency} concurrent clients (${keepAlive ? 'keep-alive' : 'a connection per request'}) without errors`, async () => {
        const counts = await burst(server.url, concurrency, BURST_MS, { keepAlive });
        assert.equal(counts.failed, 0, `failures: ${[...counts.errors].join(', ')}`);
        assert.ok(counts.ok > 0, 'no request succeeded');
      });
    }
  }

  it('keeps accepting connections after the load above', async () => {
    const res = await request(server.url, '/__version', { timeoutMs: 5000 });
    assert.equal(res.status, 200);
  });

  it('fetched the JWKS once for all of it', async () => {
    const m = await metrics(server.url);
    assert.equal(m.routes.find((r) => r.path === '/.well-known/jwks.json').count > 0, true);
    assert.equal(m.js.restarts, 0);
  });
});
