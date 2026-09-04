"""Concurrency and latency.

isère handles one connection at a time on a single httpd task, with no
keep-alive and a compile-time connection cap (ISERE_TCP_MAX_CONNECTIONS, 12 on
linux). These tests characterise what that means in practice and record the
latency distribution so regressions are visible.
"""

import statistics
import threading
import time

import pytest

import harness


def _hammer(n_clients, n_each, path="/", timeout=20.0):
    """Run n_clients threads issuing n_each requests, collecting outcomes."""
    latencies = []
    failures = []
    lock = threading.Lock()

    def worker(worker_id):
        for i in range(n_each):
            start = time.perf_counter()
            try:
                res = harness.request(path, timeout=timeout)
                elapsed = time.perf_counter() - start
                if res.status is None:
                    with lock:
                        failures.append((worker_id, i, "no status line"))
                else:
                    with lock:
                        latencies.append(elapsed)
            except OSError as exc:
                with lock:
                    failures.append((worker_id, i, repr(exc)))

    threads = [threading.Thread(target=worker, args=(w,)) for w in range(n_clients)]
    start = time.perf_counter()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    wall = time.perf_counter() - start

    return latencies, failures, wall


def _report(name, latencies, failures, wall, total):
    if latencies:
        ordered = sorted(latencies)
        p50 = statistics.median(ordered)
        p95 = ordered[min(len(ordered) - 1, int(len(ordered) * 0.95))]
        worst = ordered[-1]
    else:
        p50 = p95 = worst = float("nan")

    print(
        "\n[%s] ok=%d fail=%d wall=%.2fs throughput=%.1f req/s "
        "p50=%.1fms p95=%.1fms max=%.1fms"
        % (
            name,
            len(latencies),
            len(failures),
            wall,
            total / wall if wall else 0.0,
            p50 * 1000,
            p95 * 1000,
            worst * 1000,
        )
    )
    if failures:
        print("  first failures: %r" % (failures[:5],))


def test_serial_latency_baseline(server):
    """One client, no concurrency -- the floor for per-request latency."""
    latencies, failures, wall = _hammer(1, 50)
    _report("serial", latencies, failures, wall, 50)

    assert not failures, "serial requests should never fail: %r" % (failures[:5],)
    assert statistics.median(latencies) < 0.5, (
        "median serial latency %.1fms is implausibly high"
        % (statistics.median(latencies) * 1000)
    )


def test_moderate_concurrency_all_succeed(server):
    """8 concurrent clients stays under the 12-connection cap."""
    n_clients, n_each = 8, 10
    latencies, failures, wall = _hammer(n_clients, n_each)
    _report("concurrent-8", latencies, failures, wall, n_clients * n_each)

    assert server.alive, "server died under moderate concurrency:\n%s" % server.log()
    assert not failures, (
        "%d/%d requests failed below the connection cap: %r"
        % (len(failures), n_clients * n_each, failures[:5])
    )


def test_high_concurrency_does_not_crash(server):
    """More clients than the connection cap: failures are acceptable, a dead
    or wedged server is not."""
    n_clients, n_each = 40, 5
    latencies, failures, wall = _hammer(n_clients, n_each)
    _report("concurrent-40", latencies, failures, wall, n_clients * n_each)

    assert server.alive, "server died under load:\n%s" % server.log()
    # the server must still answer once the burst is over
    assert harness.request("/").status == 200, (
        "server stopped serving after a burst -- connection accounting may be "
        "wedged:\n%s" % server.log()
    )


def test_file_descriptors_are_not_leaked(server):
    """Sockets must be closed as connections are torn down."""
    harness.request("/")
    before = server.open_fd_count()

    for _ in range(100):
        harness.request("/")

    after = server.open_fd_count()
    print("\n[fd] before=%d after=%d" % (before, after))
    assert after <= before + 2, (
        "file descriptors leaked: %d -> %d over 100 requests" % (before, after)
    )


def test_burst_then_recover(server):
    """After a burst well over the cap, the server should return to normal."""
    _hammer(30, 3)
    time.sleep(0.5)

    latencies, failures, wall = _hammer(1, 20)
    _report("recovery", latencies, failures, wall, 20)
    assert not failures, (
        "server did not recover after a burst -- %d/20 follow-up requests "
        "failed: %r" % (len(failures), failures[:5])
    )


@pytest.mark.slow
def test_slow_handler_blocks_the_event_loop(server_factory):
    """A handler with a pending timer starves accept().

    ISERE_HTTPD_HANDLER_TIMEOUT_MS is declared but never enforced, and the
    drain loop re-queues unfinished connections onto the queue it is
    iterating, so one slow request stalls every other client. This test
    measures that rather than asserting it away.
    """
    srv = server_factory("pending_timer.js")

    def slow():
        try:
            harness.request("/", timeout=30)
        except OSError:
            pass

    t = threading.Thread(target=slow)
    t.start()
    time.sleep(0.3)

    start = time.perf_counter()
    try:
        harness.request("/", timeout=30)
    except OSError:
        pass
    blocked_for = time.perf_counter() - start
    t.join(timeout=30)

    print("\n[starvation] second client waited %.2fs" % blocked_for)
    assert srv.alive, "server died on a never-resolving handler:\n%s" % srv.log()
    assert blocked_for < 1.0, (
        "a single slow handler blocked an unrelated client for %.2fs -- the "
        "event loop is starved while a handler is pending" % blocked_for
    )
