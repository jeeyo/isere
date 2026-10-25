// Engine cleanup (criterion 10). In cold mode every request creates and frees an
// engine instance, so each way a request can leave something behind is exercised
// on every request. isère counts the objects still alive when a QuickJS runtime is
// freed (it frees the runtime only when there are none, so the gc_obj_list
// assertion can't fire); every cause gets its own test.
import { describe, it, before, after } from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';

import { startServer, metrics, isRemote } from './lib/target.mjs';
import { request } from './lib/http.mjs';

// a server that answers after a delay, for fetches still in flight at teardown
async function slowServer(delayMs) {
  const sockets = new Set();
  const server = http.createServer((req, res) => {
    setTimeout(() => { res.end('late'); }, delayMs);
  });
  server.on('connection', (s) => { sockets.add(s); s.on('close', () => sockets.delete(s)); });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  return {
    url: `http://127.0.0.1:${server.address().port}/`,
    close: () => new Promise((r) => { for (const s of sockets) s.destroy(); server.close(r); }),
  };
}

describe('engine cleanup in cold mode', { skip: isRemote && 'needs ISERE_JS_MODE on the Linux build' }, () => {
  let server;
  let slow;
  before(async () => {
    slow = await slowServer(3000);
    server = await startServer({ env: { ISERE_JS_MODE: 'cold', ISERE_HANDLER_TIMEOUT_MS: '500' } });
  });
  after(async () => {
    const result = await server.stop();
    await slow.close();
    assert.ok(!result.hung, 'server did not shut down on SIGINT');
    assert.equal(result.code, 0, `exit ${result.code} signal ${result.signal}\n${result.stderr.slice(-2000)}`);
    assert.ok(!/outlived their context|Assertion/.test(result.stderr), result.stderr.slice(-2000));
  });

  // runs `path` a few times and checks every instance was freed with nothing left in it
  async function leavesNothing(path, { status = 200, times = 3 } = {}) {
    const before = (await metrics(server.url)).js;
    for (let i = 0; i < times; i++) {
      const res = await request(server.url, path, { timeoutMs: 10000 });
      assert.equal(res.status, status, `${path}: ${res.body}`);
    }
    const after = (await metrics(server.url)).js;
    assert.equal(after.instances_created - before.instances_created, times, 'instances created');
    assert.equal(after.instances_freed - before.instances_freed, times, 'instances freed');
    assert.equal(after.leftover_objects - before.leftover_objects, 0, `${path} left objects behind`);
  }

  it('a plain request (prelude, polyfill globals, module)', () => leavesNothing('/'));
  it('a timer still pending at teardown', () => leavesNothing('/__test/timers-pending'));
  it('a timer that fired', () => leavesNothing('/__test/timer-fires'));
  it('an interval cleared from its own callback', () => leavesNothing('/__test/interval'));
  it('a promise that never settles', () => leavesNothing('/__test/promise-pending'));
  it('an unhandled rejection', () => leavesNothing('/__test/rejection-unhandled'));
  it('a handler that throws', () => leavesNothing('/__test/throw', { status: 500 }));
  it('a handler that throws asynchronously', () => leavesNothing('/__test/throw-async', { status: 500 }));
  it('a module that fails while loading', () => leavesNothing('/__test/module-error', { status: 500 }));
  it('a handler that never answers (deadline)', () => leavesNothing('/__test/never', { status: 504, times: 2 }));
  it('a busy loop interrupted at the deadline', () => leavesNothing('/__test/busy-loop', { status: 504, times: 2 }));
  it('an endless await loop interrupted at the deadline', () => leavesNothing('/__test/await-loop', { status: 504, times: 2 }));
  it('a fetch still in flight at teardown', () => leavesNothing(`/__test/fetch-pending?url=${encodeURIComponent(slow.url)}`));
  it('a finished fetch', () => leavesNothing(`/__test/fetch?url=${encodeURIComponent(server.url + '/.well-known/jwks.json')}`));
  it('crypto.subtle with imported keys', () => leavesNothing('/__test/crypto'));
  it('a full JWT verification with its JWKS fetch', async () => {
    const { sign } = await import('./lib/jwt.mjs');
    const before = (await metrics(server.url)).js;
    for (let i = 0; i < 3; i++) {
      const res = await request(server.url, '/verify', { headers: { authorization: `Bearer ${sign()}` }, timeoutMs: 10000 });
      assert.equal(res.status, 200, res.body);
    }
    const after = (await metrics(server.url)).js;
    assert.equal(after.leftover_objects - before.leftover_objects, 0);
    assert.equal(after.instances_created - after.instances_freed, 0);
  });
});

describe('engine cleanup at shutdown in warm mode', { skip: isRemote && 'reads the exit status' }, () => {
  it('frees the warm instance with nothing left in it after a mixed workload', async () => {
    const server = await startServer({ env: { ISERE_HANDLER_TIMEOUT_MS: '300' } });
    for (const path of ['/', '/__test/timers-pending', '/__test/promise-pending', '/__test/rejection-unhandled',
      '/__test/throw', '/__test/never', '/__test/interval', '/__test/crypto']) {
      await request(server.url, path, { timeoutMs: 10000 });
    }
    // a request still running when the server stops
    const running = request(server.url, '/__test/slow?ms=5000', { timeoutMs: 10000 }).catch((e) => e);
    await new Promise((r) => setTimeout(r, 50));
    const result = await server.stop();
    await running;
    assert.ok(!result.hung, 'server did not shut down on SIGINT');
    assert.equal(result.code, 0, `exit ${result.code} signal ${result.signal}\n${result.stderr.slice(-2000)}`);
    assert.ok(!/outlived their context|still alive|Assertion/.test(result.stderr), result.stderr.slice(-2000));
  });
});
