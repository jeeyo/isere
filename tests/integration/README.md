# isère integration tests

Black-box tests that run the real `isere` binary and talk to it over HTTP.

They exist to make memory and concurrency behaviour observable before it gets
fixed, so **some of these are expected to fail on `main` today**. A failure
here is a finding, not a broken test.

## What is covered

| File | Covers |
|---|---|
| `test_smoke.py` | Server starts, serves the handler, survives malformed input and abrupt disconnects |
| `test_concurrency.py` | Latency distribution, concurrent clients, fd leaks, event-loop starvation |
| `test_memory.py` | RSS stability across requests, connect/disconnect cycles, partial requests, timers, large bodies |
| `test_risky_js.py` | Hostile handler code: throwing, embedded NULs, non-string headers, oversized headers, pipelining |

`handlers/` holds the JavaScript each test deploys. The harness compiles a
`.js` file into `handler.so` with the same `xxd`/`sed` recipe the root
`CMakeLists.txt` uses, so what is tested matches a normal build.

## Requirements

- Linux (the harness reads `/proc/<pid>/status` for RSS)
- A built `linux` target (`build/isere`)
- `python3`, `pytest`, `cc`, `xxd`

## Running

Build first, then:

```bash
python3 -m pytest tests/integration -v
```

Point at a binary elsewhere with `ISERE_BIN=/path/to/isere`.

Skip the long soaks:

```bash
python3 -m pytest tests/integration -v -m "not slow"
```

Tune the leak threshold and soak length:

```bash
python3 -m pytest tests/integration --rss-tolerance-kb 512 --soak-requests 500
```

On macOS, build and run inside the container — the FreeRTOS POSIX port does
not build natively on Darwin:

```bash
docker run --rm -v "$PWD":/app -w /app <image> \
  bash -c "cd build && make -j && cd .. && python3 -m pytest tests/integration -v"
```

## Notes

- `ISERE_HTTPD_PORT` is a compile-time constant (8080), so tests run one
  server at a time and must not be parallelised.
- RSS is the leak proxy because isère uses FreeRTOS `heap_3`, which is a thin
  wrapper over libc `malloc`, so leaked allocations surface as process RSS.
- Each server runs in its own scratch directory with its own `handler.so`;
  `LD_LIBRARY_PATH` is set because `loader.c` `dlopen()`s by relative name.
