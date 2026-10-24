// Tens of thousands of requests on one server. JerryScript used to stop accepting
// connections after about 29,000 requests under wrk; this goes well past that,
// with both keep-alive and a connection per request, and checks the heap ends
// where it started.
import { describe, it, before, after } from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';

import { startServer, metrics, isRemote } from './lib/target.mjs';
import { request } from './lib/http.mjs';

const REQUESTS = Number(process.env.ISERE_ENDURANCE_REQUESTS || 40000);
const CLIENTS = 4;

// a lean keep-alive client: one request in flight per socket, Content-Length bodies
function client(url) {
  const { hostname, port } = new URL(url);
  const socket = net.connect({ host: hostname, port: Number(port) });
  socket.setNoDelay(true);
  let buf = Buffer.alloc(0);
  let waiting = null;
  socket.on('data', (d) => {
    buf = buf.length ? Buffer.concat([buf, d]) : d;
    const end = buf.indexOf('\r\n\r\n');
    if (end < 0 || !waiting) return;
    const head = buf.subarray(0, end).toString('latin1');
    const len = Number(/\r\ncontent-length: (\d+)/i.exec(head)?.[1] ?? 0);
    if (buf.length < end + 4 + len) return;
    buf = buf.subarray(end + 4 + len);
    const w = waiting;
    waiting = null;
    w.resolve(Number(head.slice(9, 12)));
  });
  const fail = (e) => { if (waiting) { const w = waiting; waiting = null; w.reject(e); } };
  socket.on('error', fail);
  socket.on('close', () => fail(new Error('connection closed')));
  const ready = new Promise((r) => socket.once('connect', r));
  return {
    async get(path) {
      await ready;
      return new Promise((resolve, reject) => {
        waiting = { resolve, reject };
        socket.write(`GET ${path} HTTP/1.1\r\nHost: x\r\n\r\n`);
      });
    },
    close: () => socket.destroy(),
  };
}

describe('endurance', { skip: isRemote && 'takes too long on a board' }, () => {
  let server;
  before(async () => { server = await startServer({ env: { ISERE_NDJSON: '0' } }); });
  after(async () => {
    const result = await server.stop();
    assert.ok(!result.hung, 'server did not shut down on SIGINT');
  });

  it(`serves ${REQUESTS} keep-alive requests and keeps accepting connections`, async () => {
    const heap = async () => JSON.parse((await request(server.url, '/__metrics?gc=1')).body);
    const before = await heap();
    const clients = Array.from({ length: CLIENTS }, () => client(server.url));
    let next = 0;
    let errors = 0;
    await Promise.all(clients.map(async (c) => {
      while (next < REQUESTS) {
        next++;
        const status = await c.get(next % 2 ? '/' : '/.well-known/jwks.json').catch(() => -1);
        if (status !== 200) errors++;
      }
    }));
    clients.forEach((c) => c.close());
    assert.equal(errors, 0);
    assert.equal((await request(server.url, '/__version', { timeoutMs: 3000 })).status, 200);

    const after = await heap();
    for (const [kind, b, a] of [['device', before.device.heap_in_use, after.device.heap_in_use],
      ['engine', before.js.heap_used, after.js.heap_used]]) {
      const perRequest = (a - b) / REQUESTS;
      assert.ok(Math.abs(perRequest) < 1, `${kind} heap changed by ${perRequest.toFixed(3)} bytes per request`);
    }
  });

  it('serves 5000 more with a connection per request', async () => {
    for (let i = 0; i < 5000; i += 4) {
      const res = await Promise.all(Array.from({ length: 4 }, () => request(server.url, '/', { timeoutMs: 3000 })));
      for (const r of res) assert.equal(r.status, 200);
    }
    const m = await metrics(server.url);
    assert.equal(m.js.restarts, 0);
  });
});
