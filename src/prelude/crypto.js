// crypto.subtle (importKey, verify, digest) and getRandomValues, part of isère's prelude, compiled the first time a handler touches it.
// See src/prelude.js: `g` is the global object, `native` the engine's natives,
// `shared` helpers from the core prelude.
(function (g, native, shared) {
  'use strict';

  var toBytes = shared.toBytes;
  var DOMException = shared.DOMException;

  // ----------------------------------------------------------------- crypto

  var keyData = new WeakMap();

  function CryptoKey() { throw new TypeError('Illegal constructor'); }

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

  var crypto = {
    subtle: subtle,
    getRandomValues: function (array) {
      native.randomFill(toBytes(array));
      return array;
    },
  };

  return { crypto: crypto, CryptoKey: CryptoKey };
})
