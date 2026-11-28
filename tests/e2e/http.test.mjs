// In-place HTTP parsing (criterion 3): budgets, framing, pipelining, and that
// every byte of a request reaches the handler intact.
import { describe, it, before, after } from 'node:test';
import assert from 'node:assert/strict';

import { startServer } from './lib/target.mjs';
import { request, rawRequest } from './lib/http.mjs';

const HEAD_BUDGET = 8 * 1024;
const BODY_BUDGET = 16 * 1024;

function echo(res) {
  assert.equal(res.status, 200, res.body.toString());
  return JSON.parse(res.body.toString());
}

describe('HTTP parsing', () => {
  let server;
  before(async () => { server = await startServer(); });
  after(async () => {
    const result = await server.stop();
    assert.ok(!result.hung, 'server did not shut down on SIGINT');
  });

  it('passes long header values through intact (realistic JWTs)', async () => {
    const value = 'Bearer ' + 'a'.repeat(1500) + '.' + 'b'.repeat(1500);
    const res = await request(server.url, '/__test/echo', { headers: { authorization: value } });
    assert.equal(echo(res).headers.authorization, value);
  });

  it('passes many headers through', async () => {
    const headers = {};
    for (let i = 0; i < 60; i++) headers[`x-h${i}`] = `value-${i}-${'v'.repeat(40)}`;
    const got = echo(await request(server.url, '/__test/echo', { headers })).headers;
    for (let i = 0; i < 60; i++) assert.equal(got[`x-h${i}`], headers[`x-h${i}`]);
  });

  it('passes the method, path, query and body through', async () => {
    const body = JSON.stringify({ hello: 'world', n: [1, 2, 3] });
    const got = echo(await request(server.url, '/__test/echo?a=1&b=two', {
      method: 'POST', body, headers: { 'content-type': 'application/json' },
    }));
    assert.equal(got.method, 'POST');
    assert.equal(got.path, '/__test/echo');
    assert.equal(got.query, '?a=1&b=two');
    assert.equal(got.body, body);
  });

  it('decodes a chunked body in place', async () => {
    const payload = 'POST /__test/echo HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n' +
      '5\r\nhello\r\n1;ext=1\r\n \r\n5\r\nworld\r\n0\r\nTrailer: x\r\n\r\n';
    const { responses } = await rawRequest(server.url, payload);
    assert.equal(responses[0]?.status, 200);
    assert.equal(JSON.parse(responses[0].body.toString()).body, 'hello world');
  });

  it('answers 431 when the head exceeds its budget', async () => {
    const payload = 'GET /__test/echo HTTP/1.1\r\nHost: x\r\nX-Big: ' + 'x'.repeat(HEAD_BUDGET) + '\r\n\r\n';
    const { responses } = await rawRequest(server.url, payload);
    assert.equal(responses[0]?.status, 431);
  });

  it('answers 413 when Content-Length exceeds the body budget', async () => {
    const payload = `POST /__test/echo HTTP/1.1\r\nHost: x\r\nContent-Length: ${BODY_BUDGET + 1}\r\n\r\n`;
    const { responses } = await rawRequest(server.url, payload);
    assert.equal(responses[0]?.status, 413);
  });

  it('answers 413 when a chunked body exceeds the body budget', async () => {
    const chunk = 'x'.repeat(4096);
    let payload = 'POST /__test/echo HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n';
    for (let i = 0; i < 6; i++) payload += `1000\r\n${chunk}\r\n`;
    payload += '0\r\n\r\n';
    const { responses } = await rawRequest(server.url, payload);
    assert.equal(responses[0]?.status, 413);
  });

  it('accepts a body right at the budget', async () => {
    const body = 'y'.repeat(BODY_BUDGET);
    const got = echo(await request(server.url, '/__test/echo', { method: 'POST', body }));
    assert.equal(got.body.length, BODY_BUDGET);
  });

  it('rejects Content-Length together with Transfer-Encoding (400)', async () => {
    const payload = 'POST /__test/echo HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n';
    const { responses } = await rawRequest(server.url, payload);
    assert.equal(responses[0]?.status, 400);
  });

  it('rejects duplicate Content-Length headers (400), even when equal', async () => {
    const payload = 'POST /__test/echo HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\nhello';
    const { responses } = await rawRequest(server.url, payload);
    assert.equal(responses[0]?.status, 400);
  });

  it('rejects obsolete line folding and bare control characters (400)', async () => {
    for (const payload of [
      'GET /__test/echo HTTP/1.1\r\nHost: x\r\nX-A: a\r\n b\r\n\r\n',
      'GET /__test/echo HTTP/1.1\r\nHost: x\r\nX-A: a\x00b\r\n\r\n',
      'GET /__test/echo HTTP/1.1\r\nHost: x\r\n: empty-name\r\n\r\n',
      'GET /__test/echo HTTP/1.1\nHost: x\n\n',
    ]) {
      const { responses } = await rawRequest(server.url, payload);
      assert.equal(responses[0]?.status, 400, JSON.stringify(payload));
    }
  });

  it('parses two requests arriving in one segment, one after the other', async () => {
    const one = 'GET /__test/echo?n=1 HTTP/1.1\r\nHost: x\r\n\r\n';
    const two = 'POST /__test/echo?n=2 HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\nConnection: close\r\n\r\nabc';
    const { responses } = await rawRequest(server.url, one + two, { untilResponses: 2 });
    assert.equal(responses.length, 2);
    assert.equal(JSON.parse(responses[0].body.toString()).query, '?n=1');
    const second = JSON.parse(responses[1].body.toString());
    assert.equal(second.query, '?n=2');
    assert.equal(second.body, 'abc');
  });

  it('parses a request that arrives a byte at a time', async () => {
    const { hostname, port } = new URL(server.url);
    const net = await import('node:net');
    const payload = 'GET /__test/echo?slow=1 HTTP/1.1\r\nHost: x\r\nX-Long: ' + 'z'.repeat(700) + '\r\nConnection: close\r\n\r\n';
    const data = await new Promise((resolve, reject) => {
      const socket = net.connect({ host: hostname, port: Number(port) });
      socket.setNoDelay(true);
      let out = '';
      socket.on('data', (d) => { out += d; });
      socket.on('end', () => resolve(out));
      socket.on('error', reject);
      let i = 0;
      const tick = () => {
        // a few bytes per write, so the head is re-parsed many times as it grows
        socket.write(payload.slice(i, i + 7));
        i += 7;
        if (i < payload.length) setTimeout(tick, 1);
      };
      socket.on('connect', tick);
    });
    assert.match(data, /^HTTP\/1\.1 200/);
    assert.match(data, /"x-long":"z{700}"/);
  });

  it('uses memory proportional to the request', async () => {
    const conn = (res) => Number(/conn;desc="(\d+)"/.exec(res.headers['server-timing'])[1]);
    const small = conn(await request(server.url, '/__test/echo'));
    const large = conn(await request(server.url, '/__test/echo', { method: 'POST', body: 'q'.repeat(12000) }));
    // a small request fits the initial buffer; nothing reserves the worst case up front
    assert.ok(small < 4096, `a small request held ${small} bytes`);
    assert.ok(large > 12000 && large < 12000 * 4, `a 12 KB request held ${large} bytes`);
  });

  it('keeps answering after all of the above', async () => {
    const res = await request(server.url, '/__version');
    assert.equal(res.status, 200);
  });
});
