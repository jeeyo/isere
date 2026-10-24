import http from 'node:http';
import net from 'node:net';

// One request on a fresh connection (isère closes every connection after responding).
export function request(baseUrl, path, { method = 'GET', headers = {}, body, timeoutMs = 5000 } = {}) {
  return new Promise((resolve, reject) => {
    const req = http.request(baseUrl + path, { method, headers, agent: false }, (res) => {
      let data = '';
      res.setEncoding('utf8');
      res.on('data', (chunk) => { data += chunk; });
      res.on('end', () => resolve({ status: res.statusCode, headers: res.headers, body: data }));
      res.on('error', reject);
    });
    req.setTimeout(timeoutMs, () => req.destroy(new Error(`timed out after ${timeoutMs} ms: ${method} ${path}`)));
    req.on('error', reject);
    if (body !== undefined) req.write(body);
    req.end();
  });
}

// Sends raw bytes and collects whatever comes back until the server closes or the timeout.
export function rawRequest(baseUrl, payload, { timeoutMs = 3000, closeAfterWrite = false } = {}) {
  const { hostname, port } = new URL(baseUrl);
  return new Promise((resolve) => {
    const socket = net.connect({ host: hostname, port: Number(port) });
    let data = '';
    const done = (reason) => {
      socket.destroy();
      resolve({ data, reason });
    };
    socket.setTimeout(timeoutMs, () => done('timeout'));
    socket.on('connect', () => {
      socket.write(payload);
      if (closeAfterWrite) done('client-closed');
    });
    socket.on('data', (d) => { data += d; });
    socket.on('end', () => done('server-closed'));
    socket.on('error', (err) => done(err.code || err.message));
  });
}
