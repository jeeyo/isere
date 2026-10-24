// isère's JS prelude, evaluated by both engines before any app module.
//
// The engine binding provides one object of natives (`__isere`, removed from the
// global object once this has run). Everything handlers see is built here, once
// for both engines: console, timers, TextEncoder/TextDecoder, atob/btoa,
// Headers/Request/Response, URL, fetch and a crypto.subtle subset, following the
// Cloudflare Workers API so handlers stay portable.
//
// Two handler styles are supported:
//   export default { async fetch(request, env, ctx) { return new Response(...) } }   (Workers)
//   export const handler = async (event, context) => ({ statusCode, headers, body }) (Lambda)
//
// Written for JerryScript's ES2020 subset: no optional chaining or nullish coalescing.
(function (native) {
  'use strict';

  var g = globalThis;

  var env = JSON.parse(native.config);
  if (native.overrides) {
    var overrides = JSON.parse(native.overrides);
    for (var key in overrides) env[key] = overrides[key];
  }

  // ---------------------------------------------------------------- console

  function format(v) {
    if (typeof v === 'string') return v;
    if (v instanceof Error) return v.stack ? String(v) + '\n' + v.stack : String(v);
    if (typeof v === 'function') return '[Function]';
    if (v !== null && typeof v === 'object') {
      try { return JSON.stringify(v); } catch (e) { return String(v); }
    }
    return String(v);
  }

  function logger(level) {
    return function () {
      var parts = [];
      for (var i = 0; i < arguments.length; i++) parts.push(format(arguments[i]));
      native.log(level, parts.join(' '));
    };
  }

  g.console = {
    log: logger(0), info: logger(0), debug: logger(0), trace: logger(0),
    warn: logger(1), error: logger(2),
  };

  // ----------------------------------------------------------------- timers

  function timer(repeat) {
    return function (fn, ms) {
      if (typeof fn !== 'function') throw new TypeError('callback is not a function');
      var args = Array.prototype.slice.call(arguments, 2);
      var cb = args.length ? function () { fn.apply(undefined, args); } : fn;
      var delay = Number(ms);
      return native.setTimer(cb, delay > 0 ? delay : 0, repeat);
    };
  }

  g.setTimeout = timer(false);
  g.setInterval = timer(true);
  g.clearTimeout = g.clearInterval = function (id) {
    if (typeof id === 'number' && id > 0) native.clearTimer(id);
  };
  g.queueMicrotask = function (fn) { Promise.resolve().then(fn); };
  g.performance = { now: function () { return native.now(); } };

  // ------------------------------------------------------------------ bytes

  function toBytes(data) {
    if (data === undefined || data === null) return new Uint8Array(0);
    if (data instanceof Uint8Array) return data;
    if (data instanceof ArrayBuffer) return new Uint8Array(data);
    if (ArrayBuffer.isView(data)) return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    return native.utf8Encode(String(data));
  }

  function TextEncoder() {}
  TextEncoder.prototype.encoding = 'utf-8';
  TextEncoder.prototype.encode = function (s) { return native.utf8Encode(s === undefined ? '' : String(s)); };
  g.TextEncoder = TextEncoder;

  function TextDecoder(label) {
    if (label !== undefined && !/^utf-?8$/i.test(String(label))) throw new RangeError('only utf-8 is supported');
  }
  TextDecoder.prototype.encoding = 'utf-8';
  TextDecoder.prototype.decode = function (data) {
    return data === undefined ? '' : native.utf8Decode(toBytes(data));
  };
  g.TextDecoder = TextDecoder;

  // binary strings <-> base64, in C: a JS loop over every character is slow on both engines
  g.atob = function (s) {
    try { return native.atob(String(s)); } catch (e) { throw DOMException('invalid base64', 'InvalidCharacterError'); }
  };
  g.btoa = function (s) {
    try { return native.btoa(String(s)); } catch (e) { throw DOMException('characters outside Latin-1', 'InvalidCharacterError'); }
  };

  // ---------------------------------------------------------------- Headers

  function Headers(init) {
    this._names = [];
    this._values = [];
    if (init instanceof Headers) {
      for (var i = 0; i < init._names.length; i++) this.append(init._names[i], init._values[i]);
    } else if (Array.isArray(init)) {
      for (var j = 0; j < init.length; j++) this.append(init[j][0], init[j][1]);
    } else if (init !== undefined && init !== null) {
      var keys = Object.keys(init);
      for (var k = 0; k < keys.length; k++) {
        var v = init[keys[k]];
        if (v !== undefined && v !== null) this.append(keys[k], v);
      }
    }
  }
  Headers.prototype._find = function (name) {
    return this._names.indexOf(String(name).toLowerCase());
  };
  Headers.prototype.append = function (name, value) {
    var i = this._find(name);
    if (i >= 0) this._values[i] += ', ' + String(value);
    else { this._names.push(String(name).toLowerCase()); this._values.push(String(value)); }
  };
  Headers.prototype.set = function (name, value) {
    var i = this._find(name);
    if (i >= 0) this._values[i] = String(value);
    else this.append(name, value);
  };
  Headers.prototype.get = function (name) {
    var i = this._find(name);
    return i >= 0 ? this._values[i] : null;
  };
  Headers.prototype.has = function (name) { return this._find(name) >= 0; };
  Headers.prototype['delete'] = function (name) {
    var i = this._find(name);
    if (i >= 0) { this._names.splice(i, 1); this._values.splice(i, 1); }
  };
  Headers.prototype.forEach = function (fn, thisArg) {
    for (var i = 0; i < this._names.length; i++) fn.call(thisArg, this._values[i], this._names[i], this);
  };
  Headers.prototype.entries = function () {
    var out = [];
    for (var i = 0; i < this._names.length; i++) out.push([this._names[i], this._values[i]]);
    return out[Symbol.iterator]();
  };
  Headers.prototype.keys = function () { return this._names.slice()[Symbol.iterator](); };
  Headers.prototype.values = function () { return this._values.slice()[Symbol.iterator](); };
  Headers.prototype[Symbol.iterator] = Headers.prototype.entries;
  g.Headers = Headers;

  // ------------------------------------------------------- Request, Response

  function bodyText(body) {
    if (body === null || body === undefined) return '';
    if (typeof body === 'string') return body;
    return native.utf8Decode(toBytes(body));
  }

  function bodyBytes(body) {
    var bytes = toBytes(body);
    return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
  }

  var BodyMixin = {
    text: function () { this.bodyUsed = true; return Promise.resolve(bodyText(this._body)); },
    json: function () {
      this.bodyUsed = true;
      var body = this._body;
      return new Promise(function (resolve) { resolve(JSON.parse(bodyText(body))); });
    },
    arrayBuffer: function () { this.bodyUsed = true; return Promise.resolve(bodyBytes(this._body)); },
  };

  function Response(body, init) {
    init = init || {};
    this.status = init.status === undefined ? 200 : init.status;
    this.statusText = init.statusText === undefined ? '' : String(init.statusText);
    this.headers = new Headers(init.headers);
    this._body = body === undefined ? null : body;
    this.bodyUsed = false;
    if (typeof body === 'string' && !this.headers.has('content-type')) {
      this.headers.set('content-type', 'text/plain;charset=UTF-8');
    }
  }
  Object.defineProperty(Response.prototype, 'ok', {
    get: function () { return this.status >= 200 && this.status < 300; },
  });
  Response.prototype.text = BodyMixin.text;
  Response.prototype.json = BodyMixin.json;
  Response.prototype.arrayBuffer = BodyMixin.arrayBuffer;
  Response.json = function (data, init) {
    var r = new Response(JSON.stringify(data), init);
    r.headers.set('content-type', 'application/json');
    return r;
  };
  g.Response = Response;

  function Request(input, init) {
    init = init || {};
    if (input instanceof Request) {
      this.url = input.url;
      this.method = input.method;
      this.headers = new Headers(input.headers);
      this._body = input._body;
    } else {
      this.url = String(input);
      this.method = 'GET';
      this.headers = new Headers();
      this._body = null;
    }
    if (init.method !== undefined) this.method = String(init.method).toUpperCase();
    if (init.headers !== undefined) this.headers = new Headers(init.headers);
    if (init.body !== undefined) this._body = init.body;
    this.bodyUsed = false;
  }
  Request.prototype.text = BodyMixin.text;
  Request.prototype.json = BodyMixin.json;
  Request.prototype.arrayBuffer = BodyMixin.arrayBuffer;
  g.Request = Request;

  // -------------------------------------------------------------------- URL

  function decode(s) {
    try { return decodeURIComponent(s.replace(/\+/g, ' ')); } catch (e) { return s; }
  }

  function URLSearchParams(init) {
    this._pairs = [];
    var s = init === undefined || init === null ? '' : String(init);
    if (s[0] === '?') s = s.slice(1);
    var parts = s ? s.split('&') : [];
    for (var i = 0; i < parts.length; i++) {
      if (!parts[i]) continue;
      var eq = parts[i].indexOf('=');
      this._pairs.push(eq < 0 ? [decode(parts[i]), ''] : [decode(parts[i].slice(0, eq)), decode(parts[i].slice(eq + 1))]);
    }
  }
  URLSearchParams.prototype.get = function (name) {
    for (var i = 0; i < this._pairs.length; i++) if (this._pairs[i][0] === name) return this._pairs[i][1];
    return null;
  };
  URLSearchParams.prototype.getAll = function (name) {
    var out = [];
    for (var i = 0; i < this._pairs.length; i++) if (this._pairs[i][0] === name) out.push(this._pairs[i][1]);
    return out;
  };
  URLSearchParams.prototype.has = function (name) { return this.get(name) !== null; };
  URLSearchParams.prototype.toString = function () {
    return this._pairs.map(function (p) { return encodeURIComponent(p[0]) + '=' + encodeURIComponent(p[1]); }).join('&');
  };
  g.URLSearchParams = URLSearchParams;

  function URL(url, base) {
    url = String(url);
    var m = /^([a-zA-Z][a-zA-Z0-9+.-]*:)\/\/([^/?#]*)([^?#]*)(\?[^#]*)?(#.*)?$/.exec(url);
    if (!m && base !== undefined) {
      var b = new URL(base);
      if (url[0] === '/') return new URL(b.origin + url);
      return new URL(b.origin + b.pathname.replace(/[^/]*$/, '') + url);
    }
    if (!m) throw new TypeError('Invalid URL: ' + url);
    this.protocol = m[1].toLowerCase();
    this.host = m[2];
    var colon = m[2].lastIndexOf(':');
    this.hostname = colon >= 0 ? m[2].slice(0, colon) : m[2];
    this.port = colon >= 0 ? m[2].slice(colon + 1) : '';
    this.pathname = m[3] || '/';
    this.search = m[4] && m[4] !== '?' ? m[4] : '';
    this.hash = m[5] || '';
    this.origin = this.protocol + '//' + this.host;
    this.href = this.origin + this.pathname + this.search + this.hash;
    this.searchParams = new URLSearchParams(this.search);
  }
  URL.prototype.toString = function () { return this.href; };
  g.URL = URL;

  // ------------------------------------------------------------------ fetch

  g.fetch = function (input, init) {
    return new Promise(function (resolve, reject) {
      var req = new Request(input, init);
      var flat = [];
      req.headers.forEach(function (v, k) { flat.push(k, v); });
      var body = req._body;
      if (body !== null && body !== undefined && typeof body !== 'string') body = toBytes(body);
      native.fetch(req.method, req.url, flat, body === null ? undefined : body,
        function (err, status, headers, data) {
          if (err) {
            reject(new TypeError('fetch failed: ' + err));
            return;
          }
          var h = new Headers();
          for (var i = 0; i + 1 < headers.length; i += 2) h.append(headers[i], headers[i + 1]);
          resolve(new Response(data, { status: status, headers: h }));
        });
    });
  };

  // ----------------------------------------------------------------- crypto

  function DOMException(message, name) {
    var e = new Error(message);
    e.name = name || 'Error';
    return e;
  }

  var keyData = new WeakMap();

  function CryptoKey() { throw new TypeError('Illegal constructor'); }
  g.CryptoKey = CryptoKey;

  function makeKey(algorithm, extractable, usages, data) {
    var key = Object.create(CryptoKey.prototype);
    key.type = 'public';
    key.algorithm = algorithm;
    key.extractable = !!extractable;
    key.usages = usages || [];
    keyData.set(key, data);
    return key;
  }

  function hashName(hash) {
    var name = String(typeof hash === 'object' && hash !== null ? hash.name : hash).toUpperCase();
    if (name !== 'SHA-256' && name !== 'SHA-384' && name !== 'SHA-512') {
      throw DOMException('unsupported hash ' + name, 'NotSupportedError');
    }
    return name;
  }

  function algorithmName(algorithm) {
    return String(typeof algorithm === 'string' ? algorithm : algorithm.name).toUpperCase();
  }

  var subtle = {
    importKey: function (format, jwk, algorithm, extractable, usages) {
      return new Promise(function (resolve) {
        if (format !== 'jwk') throw DOMException('only jwk keys are supported', 'NotSupportedError');
        var name = algorithmName(algorithm);
        if (name === 'RSASSA-PKCS1-V1_5') {
          if (jwk.kty !== 'RSA' || typeof jwk.n !== 'string' || typeof jwk.e !== 'string') {
            throw DOMException('not an RSA public key', 'DataError');
          }
          resolve(makeKey({ name: 'RSASSA-PKCS1-v1_5', hash: { name: hashName(algorithm.hash) } },
            extractable, usages, { kind: 'RSA', a: jwk.n, b: jwk.e }));
        } else if (name === 'ECDSA') {
          if (algorithm.namedCurve !== 'P-256' || jwk.kty !== 'EC' || jwk.crv !== 'P-256' ||
              typeof jwk.x !== 'string' || typeof jwk.y !== 'string') {
            throw DOMException('not a P-256 public key', 'DataError');
          }
          resolve(makeKey({ name: 'ECDSA', namedCurve: 'P-256' }, extractable, usages,
            { kind: 'EC', a: jwk.x, b: jwk.y }));
        } else {
          throw DOMException('unsupported algorithm ' + name, 'NotSupportedError');
        }
      });
    },

    verify: function (algorithm, key, signature, data) {
      return new Promise(function (resolve) {
        var k = keyData.get(key);
        if (!k) throw new TypeError('not a CryptoKey');
        var name = algorithmName(algorithm);
        var hash;
        if (name === 'RSASSA-PKCS1-V1_5' && k.kind === 'RSA') hash = key.algorithm.hash.name;
        else if (name === 'ECDSA' && k.kind === 'EC') hash = hashName(algorithm.hash);
        else throw DOMException('key does not match ' + name, 'InvalidAccessError');
        resolve(native.verify(k.kind, hash, k.a, k.b, toBytes(signature), toBytes(data)));
      });
    },

    digest: function (algorithm, data) {
      return new Promise(function (resolve) {
        var bytes = native.digest(hashName(algorithm), toBytes(data));
        resolve(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength));
      });
    },
  };

  g.crypto = {
    subtle: subtle,
    getRandomValues: function (array) {
      native.randomFill(toBytes(array));
      return array;
    },
  };

  // ------------------------------------------------- calibration (criterion 6)

  g.isere = {
    // busy-loops for `ms` milliseconds of wall time
    spin: function (ms) { native.spin(Number(ms)); },
    // allocates and frees `bytes` through the engine's allocator
    alloc: function (bytes) { native.alloc(Number(bytes)); },
  };

  // --------------------------------------------------------------- dispatch

  var modules = [];
  var STATUS_TEXT_DEFAULT = 200;

  function sendError(id, err) {
    console.error('handler failed:', err);
    native.respond(id, 500, ['content-type', 'text/plain'], 'internal error\n');
  }

  function validStatus(status) {
    if (typeof status === 'number' && status % 1 === 0 && status >= 100 && status <= 599) return status;
    console.error('invalid status code', status);
    return 500;
  }

  function send(id, res) {
    var flat = [];
    var status, body;
    if (res instanceof Response) {
      status = validStatus(res.status);
      res.headers.forEach(function (v, k) { flat.push(k, v); });
      body = res._body;
    } else if (res !== null && typeof res === 'object') {
      // Lambda-style result
      status = res.statusCode === undefined ? STATUS_TEXT_DEFAULT : validStatus(res.statusCode);
      var headers = res.headers;
      if (headers !== null && typeof headers === 'object') {
        var keys = Object.keys(headers);
        for (var i = 0; i < keys.length; i++) {
          var v = headers[keys[i]];
          if (v !== undefined && v !== null) flat.push(keys[i], String(v));
        }
      }
      body = res.body;
      if (body !== null && typeof body === 'object' && !(body instanceof ArrayBuffer) && !ArrayBuffer.isView(body)) {
        body = JSON.stringify(body);
      }
    } else if (typeof res === 'string') {
      status = 200;
      flat.push('content-type', 'text/plain;charset=UTF-8');
      body = res;
    } else {
      status = 200;
      body = undefined;
    }

    if (body === null || body === undefined) body = undefined;
    else if (typeof body !== 'string') {
      body = (body instanceof ArrayBuffer || ArrayBuffer.isView(body)) ? toBytes(body) : String(body);
    }
    native.respond(id, status, flat, body);
  }

  var ctx = {
    waitUntil: function (p) { Promise.resolve(p).then(undefined, function (e) { console.error(e); }); },
    passThroughOnException: function () {},
  };

  g.__isere_register = function (index, ns) {
    modules[index] = ns;
  };

  g.__isere_dispatch = function (index, id, method, path, query, flat, body) {
    var ns = modules[index];
    var result;
    try {
      if (ns && ns['default'] && typeof ns['default'].fetch === 'function') {
        var headers = new Headers();
        for (var i = 0; i + 1 < flat.length; i += 2) headers.append(flat[i], flat[i + 1]);
        var host = headers.get('host') || 'localhost';
        var request = new Request('http://' + host + path + (query ? '?' + query : ''), {
          method: method,
          headers: headers,
          body: method === 'GET' || method === 'HEAD' ? null : body,
        });
        result = ns['default'].fetch(request, env, ctx);
      } else if (ns && typeof ns.handler === 'function') {
        var obj = {};
        for (var j = 0; j + 1 < flat.length; j += 2) {
          obj[flat[j]] = obj[flat[j]] === undefined ? flat[j + 1] : obj[flat[j]] + ', ' + flat[j + 1];
        }
        var parsed = body;
        try {
          var json = JSON.parse(body);
          if (json !== null && typeof json === 'object') parsed = json;
        } catch (e) {}
        var event = { httpMethod: method, path: path, query: query, headers: obj, body: parsed, isBase64Encoded: false };
        var context = {
          functionName: 'handler', functionVersion: '1', memoryLimitInMB: 0,
          callbackWaitsForEmptyEventLoop: false, env: env,
        };
        result = new Promise(function (resolve, reject) {
          var r = ns.handler(event, context, resolve);
          if (r !== undefined && r !== null && typeof r.then === 'function') r.then(resolve, reject);
          else if (r !== undefined) resolve(r);
        });
      } else {
        throw new Error('module exports neither default.fetch nor handler');
      }
    } catch (e) {
      sendError(id, e);
      return;
    }
    Promise.resolve(result).then(function (res) {
      try { send(id, res); } catch (e) { sendError(id, e); }
    }, function (err) { sendError(id, err); });
  };
})(__isere);
