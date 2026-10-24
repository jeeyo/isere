import { describe, it, before, after } from 'node:test';
import assert from 'node:assert/strict';

import { startServer } from './lib/target.mjs';
import { request, rawRequest } from './lib/http.mjs';

describe('smoke', () => {
  let server;
  before(async () => { server = await startServer(); });
  after(async () => {
    const result = await server.stop();
    assert.ok(!result.hung, 'server did not shut down on SIGINT');
  });

  it('reports its build and runtime at /__version', async () => {
    const res = await request(server.url, '/__version');
    assert.equal(res.status, 200);
    assert.equal(res.headers['content-type'], 'application/json');
    const version = JSON.parse(res.body);
    assert.ok(version.build, 'build id is empty');
    assert.match(version.runtime, /^(quickjs|jerryscript)$/);
  });

  it('runs the JS handler at /', async () => {
    const res = await request(server.url, '/');
    assert.equal(res.status, 200);
  });

  it('answers 404 for an unknown path', async () => {
    const res = await request(server.url, '/does-not-exist');
    assert.equal(res.status, 404);
  });

  it('serves 50 sequential requests', async () => {
    for (let i = 0; i < 50; i++) {
      const res = await request(server.url, '/');
      assert.equal(res.status, 200, `request ${i + 1}`);
    }
  });

  it('survives a malformed request line', async () => {
    await rawRequest(server.url, 'NOT AN HTTP REQUEST\r\n\r\n');
    const res = await request(server.url, '/__version');
    assert.equal(res.status, 200);
  });

  it('answers the first of two pipelined requests and survives', async () => {
    const req = 'GET /__version HTTP/1.1\r\nHost: x\r\n\r\n';
    const { data } = await rawRequest(server.url, req + req);
    assert.match(data, /^HTTP\/1\.1 200/);
    const res = await request(server.url, '/__version');
    assert.equal(res.status, 200);
  });

  it('survives a client that disconnects mid-request', async () => {
    await rawRequest(server.url, 'GET / HTTP/1.1\r\nHost: x\r\n', { closeAfterWrite: true });
    const res = await request(server.url, '/__version');
    assert.equal(res.status, 200);
  });
});
