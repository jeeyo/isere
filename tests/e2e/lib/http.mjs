import http from 'node:http';
import net from 'node:net';

// One request on a fresh connection.
export function request(baseUrl, path, { method = 'GET', headers = {}, body, timeoutMs = 5000, agent = false } = {}) {
  return new Promise((resolve, reject) => {
    const req = http.request(baseUrl + path, { method, headers, agent }, (res) => {
      const chunks = [];
      res.on('data', (chunk) => { chunks.push(chunk); });
      res.on('end', () => {
        const raw = Buffer.concat(chunks);
        resolve({ status: res.statusCode, headers: res.headers, body: raw.toString('utf8'), raw });
      });
      res.on('error', reject);
    });
    req.setTimeout(timeoutMs, () => req.destroy(new Error(`timed out after ${timeoutMs} ms: ${method} ${path}`)));
    req.on('error', reject);
    if (body !== undefined) req.write(body);
    req.end();
  });
}

// Sends raw bytes and collects whatever comes back until the server closes,
// `untilResponses` complete responses have arrived, or the timeout.
export function rawRequest(baseUrl, payload, { timeoutMs = 3000, closeAfterWrite = false, untilResponses = 0 } = {}) {
  const { hostname, port } = new URL(baseUrl);
  return new Promise((resolve) => {
    const socket = net.connect({ host: hostname, port: Number(port) });
    let data = Buffer.alloc(0);
    let finished = false;
    const done = (reason) => {
      if (finished) return;
      finished = true;
      socket.destroy();
      resolve({ data: data.toString('latin1'), raw: data, reason, responses: parseResponses(data) });
    };
    socket.setTimeout(timeoutMs, () => done('timeout'));
    socket.on('connect', () => {
      socket.write(payload);
      if (closeAfterWrite) done('client-closed');
    });
    socket.on('data', (d) => {
      data = Buffer.concat([data, d]);
      if (untilResponses > 0 && parseResponses(data).length >= untilResponses) done('responses');
    });
    socket.on('end', () => done('server-closed'));
    socket.on('error', (err) => done(err.code || err.message));
  });
}

// Splits raw bytes into the HTTP responses they contain (Content-Length framed).
export function parseResponses(buf) {
  const out = [];
  let pos = 0;
  while (pos < buf.length) {
    const end = buf.indexOf('\r\n\r\n', pos, 'latin1');
    if (end < 0) break;
    const head = buf.subarray(pos, end).toString('latin1');
    const lines = head.split('\r\n');
    const m = /^HTTP\/1\.[01] (\d{3})/.exec(lines[0]);
    if (!m) break;
    const headers = {};
    for (const line of lines.slice(1)) {
      const i = line.indexOf(':');
      if (i > 0) headers[line.slice(0, i).toLowerCase()] = line.slice(i + 1).trim();
    }
    const len = Number(headers['content-length'] || 0);
    const bodyStart = end + 4;
    if (bodyStart + len > buf.length) break;
    out.push({ status: Number(m[1]), headers, head, body: buf.subarray(bodyStart, bodyStart + len) });
    pos = bodyStart + len;
  }
  return out;
}

// A keep-alive agent for tests that reuse connections.
export function keepAliveAgent(maxSockets = 1) {
  return new http.Agent({ keepAlive: true, maxSockets });
}
