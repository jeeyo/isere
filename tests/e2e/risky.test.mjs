// Handlers that misbehave (after #33's findings): each one gets a sane answer and
// leaves the server serving everything else.
import { describe, it, before, after } from 'node:test';
import assert from 'node:assert/strict';

import { startServer } from './lib/target.mjs';
import { request, rawRequest } from './lib/http.mjs';

describe('risky handlers', () => {
  let server;
  before(async () => { server = await startServer(); });
  after(async () => {
    const result = await server.stop();
    assert.ok(!result.hung, 'server did not shut down on SIGINT');
  });

  async function stillServing() {
    const res = await request(server.url, '/');
    assert.equal(res.status, 200, 'the server stopped serving other endpoints');
  }

  it('a throwing handler answers 500', async () => {
    assert.equal((await request(server.url, '/__test/throw')).status, 500);
    await stillServing();
  });

  it('an async throw answers 500', async () => {
    assert.equal((await request(server.url, '/__test/throw-async')).status, 500);
    await stillServing();
  });

  it('a module that fails to load answers 500', async () => {
    assert.equal((await request(server.url, '/__test/module-error')).status, 500);
    await stillServing();
  });

  it('a body with a NUL is sent whole, and nothing more', async () => {
    const res = await request(server.url, '/__test/nul-body');
    assert.equal(res.status, 200);
    assert.equal(res.raw.length, 202);
    assert.equal(res.raw[0], 0x41);
    assert.equal(res.raw[1], 0);
    assert.ok(res.raw.subarray(2).every((b) => b === 0x42), 'bytes after the NUL are not the body');
  });

  it('non-string header values are stringified, null ones dropped', async () => {
    const res = await request(server.url, '/__test/nonstring-header');
    assert.equal(res.status, 200);
    assert.equal(res.headers['x-number'], '42');
    assert.equal(res.headers['x-object'], '[object Object]');
    assert.equal(res.headers['x-null'], undefined);
    assert.equal(res.headers['x-undefined'], undefined);
  });

  it('headers with CR, LF or NUL, bad names and framing headers are dropped', async () => {
    const { data, responses } = await rawRequest(server.url, 'GET /__test/bad-header HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n');
    assert.equal(responses[0]?.status, 200);
    assert.ok(!/set-cookie/i.test(data), 'header injection reached the wire');
    assert.ok(!/x-injected|x-nul|bad name/i.test(data), 'an invalid header was sent');
    assert.equal(responses[0].headers['content-length'], '10');
    assert.equal(responses[0].headers['x-fine'], 'kept');
  });

  it('all response headers are sent, however many', async () => {
    const res = await request(server.url, '/__test/many-headers');
    assert.equal(res.status, 200);
    assert.equal(Object.keys(res.headers).filter((h) => h.startsWith('x-filler-')).length, 40);
  });

  it('an out-of-range status code answers 500', async () => {
    const res = await request(server.url, '/__test/weird-status');
    assert.equal(res.status, 500);
  });

  it('a 64 KB body arrives intact', async () => {
    const res = await request(server.url, '/__test/big-body');
    assert.equal(res.status, 200);
    assert.equal(res.raw.length, 64 * 1024);
    assert.ok(res.raw.every((b) => b === 0x5a));
  });

  it('a binary body arrives byte for byte', async () => {
    const res = await request(server.url, '/__test/binary-body');
    assert.equal(res.raw.length, 256);
    for (let i = 0; i < 256; i++) assert.equal(res.raw[i], i);
  });

  it('a handler that allocates a lot answers, or fails alone if the engine runs out of heap', async () => {
    // a few MB: fine for QuickJS on the Linux heap, more than JerryScript's 512 KB arena
    for (let i = 0; i < 3; i++) {
      const res = await request(server.url, '/__test/alloc-heavy', { timeoutMs: 10000 });
      if (server.version.runtime === 'jerryscript') assert.ok([200, 500].includes(res.status), `status ${res.status}`);
      else assert.equal(res.status, 200);
    }
    await stillServing();
  });

  it('a timer that fires before the response works', async () => {
    const res = await request(server.url, '/__test/timer-fires');
    assert.equal(res.body, 'timer fired');
  });

  it('timers still pending at the response are cancelled', async () => {
    const res = await request(server.url, '/__test/timers-pending');
    assert.equal(res.body, 'timers scheduled');
    await stillServing();
  });

  it('an interval can clear itself from its own callback', async () => {
    const res = await request(server.url, '/__test/interval');
    assert.equal(res.body, 'ticks 3');
  });

  it('a handler that never answers gets 504 at the deadline', async () => {
    const start = Date.now();
    const res = await request(server.url, '/__test/never', { timeoutMs: 10000 });
    assert.equal(res.status, 504);
    assert.ok(Date.now() - start < 5000);
  });

  it('a busy loop is interrupted with 504', async () => {
    const res = await request(server.url, '/__test/busy-loop', { timeoutMs: 10000 });
    assert.equal(res.status, 504);
    await stillServing();
  });

  it('an endless await loop is interrupted with 504', async () => {
    const res = await request(server.url, '/__test/await-loop', { timeoutMs: 10000 });
    assert.equal(res.status, 504);
    await stillServing();
  });

  it('a slow endpoint does not hold up other endpoints', async () => {
    const slow = request(server.url, '/__test/slow?ms=600', { timeoutMs: 5000 });
    const start = Date.now();
    for (let i = 0; i < 5; i++) {
      assert.equal((await request(server.url, '/')).status, 200);
    }
    assert.ok(Date.now() - start < 400, 'other requests waited for the slow one');
    assert.equal((await slow).body, 'slept 600');
  });

  it('a hung endpoint does not hold up other endpoints', async () => {
    const never = request(server.url, '/__test/never', { timeoutMs: 10000 });
    const busy = request(server.url, '/__test/await-loop', { timeoutMs: 10000 });
    for (let i = 0; i < 5; i++) {
      assert.equal((await request(server.url, '/')).status, 200);
    }
    assert.equal((await never).status, 504);
    assert.equal((await busy).status, 504);
  });

  it('the prelude does not leak its internals to handlers', async () => {
    const present = JSON.parse((await request(server.url, '/__test/globals')).body);
    assert.equal(present.__isere, false);
    assert.equal(present.__isere_dispatch, false);
    assert.equal(present.__isere_register, false);
    for (const name of ['fetch', 'crypto', 'Response', 'Request', 'Headers', 'URL', 'TextEncoder', 'setTimeout', 'console']) {
      assert.equal(present[name], true, name);
    }
  });

  it('crypto.subtle.digest, getRandomValues and UTF-8 round trips work', async () => {
    const got = JSON.parse((await request(server.url, '/__test/crypto')).body);
    assert.equal(got.sha256, 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
    assert.match(got.random, /^[0-9a-f]{32}$/);
    assert.notEqual(got.random, '0'.repeat(32));
    assert.equal(got.utf8, 'é€😀');
  });

  it('the Lambda-style handler signature still works', async () => {
    const got = JSON.parse((await request(server.url, '/__test/lambda-echo?x=1', { headers: { 'X-Case': 'Kept' } })).body);
    assert.equal(got.method, 'GET');
    assert.equal(got.path, '/__test/lambda-echo');
    assert.equal(got.query, 'x=1');
    assert.equal(got.headers['X-Case'], 'Kept');
  });
});
