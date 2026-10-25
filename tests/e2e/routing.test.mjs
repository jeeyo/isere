// Multiple endpoints (criterion 1): routed by method and path, unknown routes
// never reach an engine, a failing endpoint fails alone and frees its memory.
import { describe, it, before, after } from 'node:test';
import assert from 'node:assert/strict';

import { startServer, metrics, route, isRemote } from './lib/target.mjs';
import { request } from './lib/http.mjs';

describe('routing (warm engine)', () => {
  let server;
  before(async () => { server = await startServer(); });
  after(async () => { await server.stop(); });

  it('serves each endpoint by its own handler', async () => {
    assert.equal((await request(server.url, '/')).body, 'hello from isère\n');
    assert.equal((await request(server.url, '/verify')).status, 401);
    assert.equal((await request(server.url, '/.well-known/jwks.json')).headers['content-type'], 'application/json');
    assert.equal((await request(server.url, '/__calibrate/noop')).body, 'ok\n');
  });

  it('answers 405 for a known path with another method', async () => {
    assert.equal((await request(server.url, '/verify', { method: 'POST', body: 'x' })).status, 405);
    assert.equal((await request(server.url, '/__metrics', { method: 'DELETE' })).status, 405);
  });

  it('matches paths exactly, without the query', async () => {
    assert.equal((await request(server.url, '/verify/')).status, 404);
    assert.equal((await request(server.url, '/VERIFY')).status, 404);
    assert.equal((await request(server.url, '/?a=b')).status, 200);
  });

  it('keeps module state between requests', async () => {
    const a = JSON.parse((await request(server.url, '/__test/state')).body).count;
    const b = JSON.parse((await request(server.url, '/__test/state')).body).count;
    assert.equal(b, a + 1);
  });

  it('reports the resident memory of each endpoint', async () => {
    const m = await metrics(server.url);
    for (const path of ['/', '/verify', '/__calibrate/noop', '/__test/echo']) {
      assert.ok(route(m, path).resident > 0, `${path} reports no resident memory`);
    }
    // the static JWKS costs no engine memory
    assert.equal(route(m, '/.well-known/jwks.json').resident, 0);
    // verify.js imports jwt.js, so it costs more than the one-line hello
    assert.ok(route(m, '/verify').resident > route(m, '/').resident);
  });

  it('a throwing endpoint returns 5xx and gives its memory back', async () => {
    const heap = async () => {
      const m = JSON.parse((await request(server.url, '/__metrics?gc=1')).body);
      return { device: m.device.heap_in_use, engine: m.js.heap_used };
    };
    // settle the heap after warm-up allocations
    for (let i = 0; i < 20; i++) await request(server.url, '/__test/throw');
    const before = await heap();
    const N = 200;
    for (let i = 0; i < N; i++) assert.equal((await request(server.url, '/__test/throw')).status, 500);
    const after = await heap();
    for (const kind of ['device', 'engine']) {
      const perRequest = (after[kind] - before[kind]) / N;
      assert.ok(perRequest < 64, `${kind} heap grew ${perRequest.toFixed(1)} bytes per failed request`);
    }
    assert.ok(route(await metrics(server.url), '/__test/throw').errors >= N);
    assert.equal((await request(server.url, '/')).status, 200);
  });
});

describe('routing (cold engine)', { skip: isRemote && 'needs ISERE_JS_MODE on the Linux build' }, () => {
  let server;
  before(async () => { server = await startServer({ env: { ISERE_JS_MODE: 'cold' } }); });
  after(async () => { await server.stop(); });

  it('runs in cold mode', () => {
    assert.equal(server.version.js_mode, 'cold');
  });

  it('never creates an engine instance for unknown routes, static files or built-ins', async () => {
    const before = (await metrics(server.url)).js.instances_created;
    for (let i = 0; i < 10; i++) {
      assert.equal((await request(server.url, '/no/such/route')).status, 404);
      assert.equal((await request(server.url, '/.well-known/jwks.json')).status, 200);
      assert.equal((await request(server.url, '/__version')).status, 200);
      assert.equal((await request(server.url, '/verify', { method: 'PUT', body: '' })).status, 405);
    }
    assert.equal((await metrics(server.url)).js.instances_created, before);
  });

  it('creates and frees one instance per JS request', async () => {
    const before = (await metrics(server.url)).js;
    for (let i = 0; i < 5; i++) assert.equal((await request(server.url, '/')).status, 200);
    const after = (await metrics(server.url)).js;
    assert.equal(after.instances_created - before.instances_created, 5);
    assert.equal(after.instances_freed - before.instances_freed, 5);
  });

  it('starts every request from fresh module state', async () => {
    for (let i = 0; i < 3; i++) {
      assert.equal(JSON.parse((await request(server.url, '/__test/state')).body).count, 1);
    }
  });
});
