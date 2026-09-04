"""Memory stability across connect/disconnect cycles.

isère is built with FreeRTOS heap_3, a thin wrapper over libc malloc, so
anything the server leaks shows up as process RSS. Each test warms up first
(so allocator arenas and the first JS runtime are already paid for), takes a
baseline, then soaks and compares.
"""

import time

import pytest

import harness

AREA = "memory"


def _warmup(n=30):
    for _ in range(n):
        harness.request("/")


def _soak(server, n, label, tolerance_kb, action=None):
    """Run `n` iterations of `action`, returning (baseline_kb, final_kb)."""
    action = action or (lambda: harness.request("/"))

    _warmup()
    time.sleep(0.2)
    baseline = server.rss_kb()

    samples = []
    for i in range(n):
        action()
        if i % max(1, n // 10) == 0:
            samples.append((i, server.rss_kb()))

    time.sleep(0.2)
    final = server.rss_kb()
    growth = final - baseline

    print(
        "\n[%s] baseline=%dKiB final=%dKiB growth=%dKiB over %d iterations "
        "(tolerance %dKiB)" % (label, baseline, final, growth, n, tolerance_kb)
    )
    print("  samples: %r" % (samples,))
    return baseline, final


@pytest.mark.case(
    id="MEM-01",
    title="RSS stable across sustained plain requests",
    area=AREA,
    severity="high",
    proves="Something in the request path is never freed, so the server exhausts its heap in service. On the RP2350 that is 520KiB total.",
    refs=["src/httpd.c:279", "src/httpd.c:367"],
)
def test_rss_stable_across_requests(server, soak_requests, rss_tolerance_kb):
    """Warm up, snapshot RSS, then serve a few hundred plain requests and
    compare. The baseline is taken after warm-up so allocator arenas and the
    first JS runtime are already paid for."""
    baseline, final = _soak(server, soak_requests, "requests", rss_tolerance_kb)
    assert server.alive
    assert final - baseline <= rss_tolerance_kb, (
        "RSS grew %dKiB over %d plain requests -- something in the request "
        "path is not being freed" % (final - baseline, soak_requests)
    )


@pytest.mark.case(
    id="MEM-02",
    title="RSS stable across connect/disconnect cycles",
    area=AREA,
    severity="high",
    proves="Connections dropped before a complete request leak their connection object (~10KiB each).",
    refs=["src/httpd.c:232", "src/httpd.c:367"],
)
def test_rss_stable_across_connect_disconnect(server, soak_requests, rss_tolerance_kb):
    """Connections that are opened and dropped without a complete request."""
    baseline, final = _soak(
        server,
        soak_requests,
        "connect-close",
        rss_tolerance_kb,
        action=lambda: harness.connect_and_close(),
    )
    assert server.alive
    assert final - baseline <= rss_tolerance_kb, (
        "RSS grew %dKiB over %d connect/disconnect cycles"
        % (final - baseline, soak_requests)
    )


@pytest.mark.case(
    id="MEM-03",
    title="RSS stable across abandoned partial requests",
    area=AREA,
    severity="high",
    proves="A half-sent request that is abandoned mid-parse leaks the connection or parser state.",
    refs=["src/httpd.c:219"],
)
def test_rss_stable_across_partial_requests(server, soak_requests, rss_tolerance_kb):
    """Half-sent requests, abandoned mid-parse."""
    baseline, final = _soak(
        server,
        soak_requests,
        "partial-request",
        rss_tolerance_kb,
        action=lambda: harness.connect_and_close(
            payload=b"GET / HTTP/1.1\r\nHost: x\r\nX-Half: "
        ),
    )
    assert server.alive
    assert final - baseline <= rss_tolerance_kb, (
        "RSS grew %dKiB over %d partial requests" % (final - baseline, soak_requests)
    )


@pytest.mark.case(
    id="MEM-04",
    title="RSS stable with 64KiB response bodies",
    area=AREA,
    severity="high",
    proves="The response body allocation is not released. It crosses module boundaries: allocated by the JS runtime, freed in httpd.c.",
    refs=["src/runtimes/quickjs/quickjs.c:285", "src/httpd.c:386"],
)
def test_rss_stable_with_large_responses(server_factory, rss_tolerance_kb):
    """Serve 100 responses of 64KiB each. The body is allocated inside the JS
    runtime and freed in httpd.c, so this checks a hand-off across modules."""
    srv = server_factory("big_body.js")
    n = 100
    baseline, final = _soak(srv, n, "big-body", rss_tolerance_kb)
    assert srv.alive
    assert final - baseline <= rss_tolerance_kb, (
        "RSS grew %dKiB over %d large responses -- the response body is not "
        "being released" % (final - baseline, n)
    )


@pytest.mark.case(
    id="MEM-05",
    title="RSS stable with an allocation-heavy handler",
    area=AREA,
    severity="high",
    proves="The per-request QuickJS runtime is not fully torn down, so each request keeps whatever the handler allocated.",
    refs=["src/runtimes/quickjs/quickjs.c:358"],
)
def test_rss_stable_with_allocating_handler(server_factory, rss_tolerance_kb):
    """A handler that allocates several MB should still release it when the
    per-request runtime is torn down."""
    srv = server_factory("alloc_heavy.js")
    n = 50
    baseline, final = _soak(srv, n, "alloc-heavy", rss_tolerance_kb)
    assert srv.alive
    assert final - baseline <= rss_tolerance_kb, (
        "RSS grew %dKiB over %d allocating requests -- the per-request JS "
        "runtime is not being fully freed" % (final - baseline, n)
    )


@pytest.mark.case(
    id="MEM-06",
    title="RSS stable when handlers schedule timers",
    area=AREA,
    severity="medium",
    proves="setTimeout leaks FreeRTOS timer control blocks. Only catches the create path; the fire path is covered by LEAK-04.",
    refs=["src/polyfills/quickjs/timer.c:74", "src/polyfills/quickjs/timer.c:118"],
)
def test_rss_stable_with_timers(server_factory, rss_tolerance_kb):
    """Each setTimeout allocates a FreeRTOS timer control block; the fire and
    teardown paths are expected to leak them."""
    srv = server_factory("timers.js")
    n = 150
    baseline, final = _soak(srv, n, "timers", rss_tolerance_kb)
    assert srv.alive
    assert final - baseline <= rss_tolerance_kb, (
        "RSS grew %dKiB over %d requests that schedule timers -- FreeRTOS "
        "timer blocks are leaking" % (final - baseline, n)
    )


@pytest.mark.case(
    id="MEM-07",
    title="RSS stable when a handler returns 40 headers",
    area=AREA,
    severity="medium",
    proves="Property-name atoms past the 16-header clamp are never freed. Handler-controlled, so a deployed function can drive it.",
    refs=["src/runtimes/quickjs/quickjs.c:245"],
)
def test_rss_stable_with_many_response_headers(server_factory, rss_tolerance_kb):
    """More headers than the 16-slot clamp; the excess atoms are never freed."""
    srv = server_factory("many_headers.js")
    n = 150
    baseline, final = _soak(srv, n, "many-headers", rss_tolerance_kb)
    assert srv.alive
    assert final - baseline <= rss_tolerance_kb, (
        "RSS grew %dKiB over %d requests returning 40 headers -- property "
        "atoms beyond the 16-header clamp are leaking" % (final - baseline, n)
    )


@pytest.mark.slow
@pytest.mark.case(
    id="MEM-08",
    title="RSS stable over a 1000-request soak",
    area=AREA,
    severity="high",
    proves="A slow leak that shorter runs cannot separate from allocator noise.",
    refs=["src/httpd.c:367"],
)
def test_rss_stable_over_long_soak(server, rss_tolerance_kb):
    """Longer run to separate a real leak from allocator noise."""
    n = 1000
    baseline, final = _soak(server, n, "long-soak", rss_tolerance_kb)
    assert server.alive
    assert final - baseline <= rss_tolerance_kb, (
        "RSS grew %dKiB over %d requests" % (final - baseline, n)
    )
