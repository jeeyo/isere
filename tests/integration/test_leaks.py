"""Leak detection under valgrind.

RSS is too coarse to see the leaks isère actually has: glibc reuses freed
blocks, and most of what leaks here stays *reachable* (a leaked FreeRTOS timer
block is still linked into a kernel list; a leaked atom is still owned by a
live runtime), so it never shows up as "definitely lost" either.

The signal that does work is **growth against request count**: run the same
workload twice with different request counts and compare. Memory that scales
with the number of requests served is a leak, whatever valgrind calls it.

These are slow -- valgrind costs roughly 20-50x -- so they are marked `slow`
and run as a separate CI job.
"""

import pytest

import harness

# Bytes of growth per request we are willing to call noise. A handful of
# allocations that are genuinely retained (metric instruments, the poll fd
# array reaching steady state) is fine; anything scaling linearly is not.
PER_REQUEST_TOLERANCE = 256

AREA = "leaks"

pytestmark = pytest.mark.slow


def _measure(handler_js, n_requests, action=None):
    """Serve `n_requests` under valgrind, then return its leak report."""
    action = action or (lambda: harness.request("/"))
    srv = harness.IsereServer(handler_js=handler_js, valgrind=True)
    srv.start()
    try:
        for i in range(n_requests):
            try:
                action()
            except OSError as exc:
                # Under valgrind only one thread runs at a time. The drain
                # loop in __httpd_task spins on isere_js_poll() without ever
                # yielding, so if a handler is waiting on a FreeRTOS timer the
                # timer daemon task can be starved indefinitely and the server
                # stops accepting.
                raise AssertionError(
                    "server stopped responding after %d/%d requests running "
                    "%s under valgrind (%r). The httpd drain loop busy-waits "
                    "without yielding, so any task it is waiting on can be "
                    "starved.\nserver log:\n%s"
                    % (i, n_requests, handler_js, exc, srv.log()[-2000:])
                )
    finally:
        srv.stop()

    report = srv.valgrind_report()
    srv.cleanup()
    return report


def _assert_no_growth(handler_js, low=20, high=200, action=None, label=""):
    lo = _measure(handler_js, low, action)
    hi = _measure(handler_js, high, action)

    def retained(report):
        return (
            report["definitely_lost"]
            + report["indirectly_lost"]
            + report["still_reachable"]
        )

    delta = retained(hi) - retained(lo)
    per_request = delta / float(high - low)

    print(
        "\n[%s] retained: %d bytes @%d req -> %d bytes @%d req "
        "(%.1f bytes/request, tolerance %d)"
        % (
            label or handler_js,
            retained(lo),
            low,
            retained(hi),
            high,
            per_request,
            PER_REQUEST_TOLERANCE,
        )
    )
    print(
        "  @%d: definitely=%d indirectly=%d possibly=%d reachable=%d errors=%d"
        % (
            high,
            hi["definitely_lost"],
            hi["indirectly_lost"],
            hi["possibly_lost"],
            hi["still_reachable"],
            hi["errors"],
        )
    )

    assert per_request <= PER_REQUEST_TOLERANCE, (
        "memory retained grows %.1f bytes per request for %s -- this scales "
        "with traffic, so the server will exhaust its heap in service"
        % (per_request, handler_js)
    )
    return hi


@pytest.mark.case(
    id="LEAK-01",
    title="No memory retained per plain request",
    area=AREA,
    severity="high",
    proves="Memory grows with every request served. At ~500 bytes/request an RP2350 with 520KiB of RAM dies after roughly a thousand requests.",
    refs=["src/httpd.c:279", "src/httpd.c:367"],
)
def test_no_leak_on_plain_requests():
    """Serve 20 and then 200 plain requests under valgrind and compare the
    bytes still retained at exit."""
    report = _assert_no_growth("hello.js", label="plain")
    assert report["definitely_lost"] == 0, (
        "%d bytes definitely lost serving plain requests"
        % report["definitely_lost"]
    )


@pytest.mark.case(
    id="LEAK-02",
    title="No memory retained per connect/disconnect",
    area=AREA,
    severity="high",
    proves="Connections that never send a complete request leak. Cheapest possible attack: open sockets and hang up.",
    refs=["src/httpd.c:232", "src/httpd.c:367"],
)
def test_no_leak_on_connect_disconnect():
    """Open and drop connections without completing a request, at two
    different counts, and compare retained bytes."""
    _assert_no_growth(
        "hello.js",
        action=lambda: harness.connect_and_close(),
        label="connect-close",
    )


@pytest.mark.case(
    id="LEAK-03",
    title="Timers still pending at teardown are freed",
    area=AREA,
    severity="medium",
    proves="Timers cancelled at context teardown leak their control block. Control case for LEAK-04.",
    refs=["src/polyfills/quickjs/timer.c:118"],
)
def test_no_leak_with_timers_that_never_fire():
    """Timers still pending at teardown are deleted correctly; this is the
    control case for the test below."""
    _assert_no_growth("timers.js", low=20, high=150, label="timers-pending")


@pytest.mark.case(
    id="LEAK-04",
    title="Timers that fire are freed",
    area=AREA,
    severity="medium",
    proves="polyfill_timer_callback() clears the slot without xTimerDelete(), so teardown skips it and the control block leaks on every fired timer.",
    refs=["src/polyfills/quickjs/timer.c:42", "src/polyfills/quickjs/timer.c:123"],
    known_issue="Fails today, but not for the leak: under valgrind thread serialisation the drain loop busy-waits without yielding and starves the timer task, so the server stops accepting before the measurement completes. Passes without valgrind.",
)
def test_no_leak_with_timers_that_fire():
    """The leak is on the fire path: the callback clears the slot without
    deleting the timer, so teardown skips it."""
    _assert_no_growth("firing_timer.js", low=20, high=150, label="timers-fired")


@pytest.mark.case(
    id="LEAK-05",
    title="No atoms retained when a handler returns 40 headers",
    area=AREA,
    severity="medium",
    proves="JS_FreeAtom only runs for the clamped 16, so atoms past the clamp leak on every response. Handler-controlled.",
    refs=["src/runtimes/quickjs/quickjs.c:245"],
)
def test_no_leak_with_many_response_headers():
    """Property-name atoms past the 16-header clamp are never freed."""
    _assert_no_growth("many_headers.js", low=20, high=150, label="many-headers")


@pytest.mark.case(
    id="LEAK-06",
    title="No memory retained when the handler throws",
    area=AREA,
    severity="high",
    proves="The exception path skips teardown, so a handler that always fails leaks a runtime per request.",
    refs=["src/runtimes/quickjs/quickjs.c:190"],
)
def test_no_leak_on_throwing_handler():
    """Compare retained bytes across two soaks of a handler that always
    throws."""
    _assert_no_growth("throws.js", low=20, high=150, label="throws")


@pytest.mark.case(
    id="LEAK-07",
    title="No invalid reads or writes under valgrind",
    area=AREA,
    severity="critical",
    proves="The server reads or writes memory it does not own. Distinct from leak accounting -- these are corruption bugs.",
    refs=["src/httpd.c", "src/runtimes/quickjs/quickjs.c"],
)
def test_no_memory_errors_under_valgrind():
    """Invalid reads/writes are always a bug, regardless of leak accounting."""
    report = _measure("hello.js", 30)
    assert report["errors"] == 0, (
        "valgrind reported %d memory errors serving plain requests:\n%s"
        % (report["errors"], report["raw"][-4000:])
    )
