// URL and URLSearchParams, part of isère's prelude, compiled the first time a handler touches it.
// See src/prelude.js: `g` is the global object, `native` the engine's natives,
// `shared` helpers from the core prelude.
(function (g, native, shared) {
  'use strict';

  var toBytes = shared.toBytes;
  var DOMException = shared.DOMException;

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

  return { URL: URL, URLSearchParams: URLSearchParams };
})
