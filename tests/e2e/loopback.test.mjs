// Criterion 4: the verifier fetches the JWKS from the same server while it is
// serving other requests, and doesn't deadlock or run out of connections when the
// pool is full. The pool is set to the Pico 2's real limit of 4.
import { describe, it, before, after } from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';

import { startServer, metrics, isRemote } from './lib/target.mjs';
import { request } from './lib/http.mjs';
import { sign } from './lib/jwt.mjs';

const POOL = 4;

// a client that takes a connection slot and holds it with an unfinished request
function holdConnection(url) {
  const { hostname, port } = new URL(url);
  return new Promise((resolve, reject) => {
    const socket = net.connect({ host: hostname, port: Number(port) }, () => {
      socket.write('GET / HTTP/1.1\r\nHost: x\r\n');
      resolve(socket);
    });
    socket.on('error', reject);
  });
}

describe('verifying through our own JWKS endpoint', { skip: isRemote && 'sets the pool size on the Linux build' }, () => {
  let server;
  before(async () => {
    server = await startServer({ env: { ISERE_MAX_CONNECTIONS: String(POOL), ISERE_HANDLER_TIMEOUT_MS: '3000' } });
  });
  after(async () => { await server.stop(); });

  it('fetches the JWKS from itself while the pool is full of verify requests', async () => {
    // four verifications with a cold cache: all four slots are taken by requests
    // that wait on the JWKS, which needs a fifth connection to this same server
    const results = await Promise.all(Array.from({ length: POOL }, () =>
      request(server.url, '/verify', { headers: { authorization: `Bearer ${sign()}` }, timeoutMs: 10000 })));
    for (const r of results) assert.equal(r.status, 200, r.body);
  });

  it('still verifies when other clients hold every slot but one', async () => {
    const held = [];
    try {
      for (let i = 0; i < POOL - 1; i++) held.push(await holdConnection(server.url));
      // force a refetch through an unknown kid, which needs the loopback connection
      const r = await request(server.url, '/verify', { headers: { authorization: `Bearer ${sign({ kid: 'unknown-kid' })}` }, timeoutMs: 10000 });
      assert.equal(r.status, 401);
      assert.equal(JSON.parse(r.body).error, 'unknown key');
      const ok = await request(server.url, '/verify', { headers: { authorization: `Bearer ${sign()}` }, timeoutMs: 10000 });
      assert.equal(ok.status, 200);
    } finally {
      for (const s of held) s.destroy();
    }
  });

  it('turns clients away with 503 instead of hanging when the pool is full', async () => {
    const held = [];
    try {
      for (let i = 0; i < POOL; i++) held.push(await holdConnection(server.url));
      const r = await request(server.url, '/', { timeoutMs: 5000 });
      assert.equal(r.status, 503);
    } finally {
      for (const s of held) s.destroy();
    }
    // and recovers as soon as slots free up
    await new Promise((r) => setTimeout(r, 100));
    assert.equal((await request(server.url, '/')).status, 200);
    assert.ok((await metrics(server.url)).connections.rejected >= 1);
  });

  it('makes room by closing idle keep-alive connections', async () => {
    // four connections that each finished a request and now sit idle
    const { hostname, port } = new URL(server.url);
    const idle = await Promise.all(Array.from({ length: POOL }, () => new Promise((resolve, reject) => {
      const socket = net.connect({ host: hostname, port: Number(port) }, () => {
        socket.write('GET / HTTP/1.1\r\nHost: x\r\n\r\n');
      });
      socket.once('data', () => resolve(socket));
      socket.on('error', reject);
    })));
    try {
      const r = await request(server.url, '/', { timeoutMs: 5000 });
      assert.equal(r.status, 200);
      assert.ok((await metrics(server.url)).connections.evicted >= 1);
    } finally {
      for (const s of idle) s.destroy();
    }
  });
});
