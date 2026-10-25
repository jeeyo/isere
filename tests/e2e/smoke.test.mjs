import { describe, it, before, after } from 'node:test';
import assert from 'node:assert/strict';

import { startServer } from './lib/target.mjs';
import { request, rawRequest, keepAliveAgent } from './lib/http.mjs';

describe('smoke', () => {
  let server;
  before(async () => { server = await startServer(); });
  after(async () => {
    const result = await server.stop();
    assert.ok(!result.hung, 'server did not shut down on SIGINT');
    if (!result.remote) assert.equal(result.code, 0, `exit code ${result.code}\n${result.stderr}`);
  });

  it('reports its build, runtime and engine mode at /__version', async () => {
    const res = await request(server.url, '/__version');
    assert.equal(res.status, 200);
    assert.equal(res.headers['content-type'], 'application/json');
    const version = JSON.parse(res.body);
    assert.ok(version.build, 'build id is empty');
    assert.match(version.runtime, /^(quickjs|jerryscript)$/);
    assert.match(version.js_mode, /^(warm|cold)$/);
  });

  it('runs the JS handler at /', async () => {
    const res = await request(server.url, '/');
    assert.equal(res.status, 200);
    assert.equal(res.body, 'hello from isère\n');
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

  it('serves 50 requests over one keep-alive connection', async () => {
    const agent = keepAliveAgent(1);
    try {
      for (let i = 0; i < 50; i++) {
        const res = await request(server.url, '/', { agent });
        assert.equal(res.status, 200, `request ${i + 1}`);
        assert.equal(res.headers.connection, 'keep-alive');
      }
    } finally {
      agent.destroy();
    }
  });

  it('answers a malformed request line with 400 and survives', async () => {
    const { responses } = await rawRequest(server.url, 'NOT AN HTTP REQUEST\r\n\r\n');
    assert.equal(responses[0]?.status, 400);
    const res = await request(server.url, '/__version');
    assert.equal(res.status, 200);
  });

  it('survives a client that disconnects mid-request', async () => {
    await rawRequest(server.url, 'GET / HTTP/1.1\r\nHost: x\r\n', { closeAfterWrite: true });
    const res = await request(server.url, '/__version');
    assert.equal(res.status, 200);
  });

  it('answers HEAD without a body', async () => {
    const { data } = await rawRequest(server.url, 'HEAD / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n');
    assert.match(data, /^HTTP\/1\.1 200 /);
    assert.match(data, /\r\nContent-Length: 18\r\n/);
    assert.ok(data.endsWith('\r\n\r\n'), 'HEAD response carried a body');
  });
});
