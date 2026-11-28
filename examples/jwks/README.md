# isère JWKS example

JWT verification against a cached JWKS, as an [isère](https://github.com/jeeyo/isere) app. It is the workload isère was built around, and isère's own firmware embeds this app (its tests add `tests/e2e/app`). The project is laid out like [`examples/hello-world`](../hello-world): it includes isère with `isere_import.cmake` and builds with `isere_add_app()`, so it can be copied out into a repository of its own.

```
CMakeLists.txt        includes isère, builds the app into firmware
isere_import.cmake    finds isère and sets up the board's toolchain (copied from isère, keep it identical)
app/routes.json       routes, and the JWKS settings as config
app/verify.js         GET /verify: checks Authorization: Bearer <jwt> against the JWKS, 200 with the claims or 401
app/jwt.js            JWT parsing and RS256/ES256 verification with crypto.subtle, and the JWKS cache
app/jwks.json         GET /.well-known/jwks.json: the board's own public keys, served from flash without JS
```

| Config | Meaning | Default |
|---|---|---|
| `jwks_url` | Where to fetch the JWKS. HTTP only on a board (no CA bundle there yet). | the app's own `/.well-known/jwks.json`, over loopback |
| `jwks_ttl_ms` | How long a fetched JWKS is trusted. | 300000 |
| `jwks_refetch_cooldown_ms` | The shortest gap between refetches when a token names an unknown key. | 10000 |

The JWKS is cached in a module-level variable, which persists between requests while the engine stays warm. A token whose `kid` isn't in the cache triggers a refetch, at most once per cooldown.

## Build and run

Inside isère's repository, isère is two directories up:

```sh
cmake -B build -DISERE_PATH=../.. -DJS_RUNTIME=jerryscript   # Linux (in Docker on a Mac)
cmake --build build -j
./build/isere_jwks &
curl -s http://127.0.0.1:8080/.well-known/jwks.json
curl -i http://127.0.0.1:8080/verify -H "authorization: Bearer <jwt>"

cmake -B build-k64f -DISERE_PATH=../.. -DISERE_BOARD=frdm-k64f -DJS_RUNTIME=jerryscript
cmake --build build-k64f -j                                  # build-k64f/isere_jwks.elf and .bin
```

In a repository of its own, add isère as the `isere/` submodule instead, as [`jeeyo/isere-hello-world`](https://github.com/jeeyo/isere-hello-world) does, and drop `-DISERE_PATH`.

The keys in `app/jwks.json` are isère's demo keys; their private halves are in isère's `tests/e2e/keys/` for the tests and protect nothing. Put your own public keys there, or point `jwks_url` at your identity provider's JWKS.

On the FRDM-K64F an RS256 verification costs about 90 ms of CPU. `tests/e2e/jwt.test.mjs` in isère tests this app on Linux and on the board.
