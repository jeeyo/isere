// fetch(), part of isère's prelude, compiled the first time a handler touches it.
// See src/prelude.js: `g` is the global object, `native` the engine's natives,
// `shared` helpers from the core prelude.
(function (g, native, shared) {
  'use strict';

  var toBytes = shared.toBytes;
  var DOMException = shared.DOMException;

  // ------------------------------------------------------------------ fetch

  function fetch(input, init) {
    return new Promise(function (resolve, reject) {
      var req = new g.Request(input, init);
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
          var h = new g.Headers();
          for (var i = 0; i + 1 < headers.length; i += 2) h.append(headers[i], headers[i + 1]);
          resolve(new g.Response(data, { status: status, headers: h }));
        });
    });
  }

  return { fetch: fetch };
})
