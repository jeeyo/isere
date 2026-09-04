"""Hostile / awkward handler code.

Every handler here is something a user of the platform could plausibly deploy.
None of it should be able to corrupt the server, leak the per-request JS
context, or take the process down.
"""

import json
import time

import harness


def _run_many(n=60):
    statuses = []
    for _ in range(n):
        try:
            statuses.append(harness.request("/").status)
        except OSError:
            statuses.append(None)
    return statuses


def test_throwing_handler_does_not_leak_context(server_factory, rss_tolerance_kb):
    srv = server_factory("throws.js")
    _run_many(30)
    time.sleep(0.2)
    baseline = srv.rss_kb()
    _run_many(150)
    time.sleep(0.2)
    growth = srv.rss_kb() - baseline

    print("\n[throws] rss growth=%dKiB" % growth)
    assert srv.alive, "server died on a throwing handler:\n%s" % srv.log()
    assert growth <= rss_tolerance_kb, (
        "RSS grew %dKiB over 150 throwing requests -- the JS runtime is not "
        "released when the handler throws" % growth
    )


def test_embedded_nul_in_body_does_not_leak_memory(server_factory):
    """The response length is taken from the JS string (which counts the NUL)
    while the copy stops at the NUL, so the server can send adjacent heap
    memory to the client."""
    srv = server_factory("embedded_nul.js")
    res = harness.request("/")

    assert srv.alive, "server died on a NUL-containing body:\n%s" % srv.log()
    assert res.status == 200

    # Everything after the NUL must be the filler the handler actually
    # produced. Anything else is memory that was never part of the response.
    assert res.body.startswith(b"A"), repr(res.body[:32])
    payload = res.body.split(b"\r\n\r\n")[0]
    leaked = payload.replace(b"A", b"", 1).replace(b"\x00", b"", 1)
    assert set(leaked) <= {ord("B")}, (
        "response body contains bytes the handler never wrote -- heap "
        "over-read. got %r" % payload[:128]
    )


def test_nonstring_header_values_are_handled(server_factory):
    srv = server_factory("nonstring_header.js")
    statuses = _run_many(20)

    assert srv.alive, (
        "server died on non-string header values -- likely strlen(NULL) in "
        "writeback:\n%s" % srv.log()
    )
    assert all(s is not None for s in statuses), (
        "some requests got no response: %r" % statuses
    )


def test_many_response_headers_are_clamped(server_factory):
    srv = server_factory("many_headers.js")
    res = harness.request("/")

    assert srv.alive
    assert res.status == 200
    filler = [k for k in res.headers if k.startswith("x-filler-")]
    print("\n[many-headers] returned %d filler headers" % len(filler))
    assert len(filler) <= 16, (
        "server emitted %d headers, above ISERE_HTTPD_MAX_HTTP_HEADERS"
        % len(filler)
    )


def test_out_of_range_status_code(server_factory):
    srv = server_factory("weird_status.js")
    res = harness.request("/")

    assert srv.alive
    print("\n[weird-status] status line -> %r" % res.raw.split(b"\r\n")[0])
    assert res.status is not None, "no parseable status line: %r" % res.raw[:64]


def test_allocating_handler_is_bounded(server_factory):
    """No per-handler memory limit is enforced today; at minimum the server
    must survive and keep answering."""
    srv = server_factory("alloc_heavy.js")
    statuses = _run_many(20)

    assert srv.alive, (
        "server died on an allocation-heavy handler:\n%s" % srv.log()
    )
    assert all(s == 200 for s in statuses), (
        "allocation-heavy requests did not all succeed: %r" % statuses
    )


def test_large_body_is_delivered_intact(server_factory):
    srv = server_factory("big_body.js")
    res = harness.request("/")

    assert srv.alive
    assert res.status == 200
    body = res.body
    if body.endswith(b"\r\n\r\n"):
        body = body[:-4]
    assert len(body) == 64 * 1024, (
        "expected a 64KiB body, got %d bytes -- response was truncated"
        % len(body)
    )
    assert set(body) == {ord("Z")}, "large body was corrupted in transit"


def test_oversized_request_header_is_rejected_or_truncated(server_factory):
    """A header value far beyond ISERE_HTTPD_MAX_HTTP_HEADER_VALUE_LEN."""
    srv = server_factory("echo.js")
    res = harness.request("/", headers={"X-Big": "q" * 4096})

    assert srv.alive, "server died on an oversized header:\n%s" % srv.log()
    assert res.status is not None
    if res.status == 200:
        echoed = json.loads(res.body.decode())
        headers = {k.lower(): v for k, v in echoed["headers"].items()}
        got = headers.get("x-big", "")
        print("\n[oversized-header] handler saw %d bytes" % len(got))
        assert len(got) < 4096, "oversized header was not truncated"


def test_many_request_headers(server_factory):
    """More request headers than the 16-slot array."""
    srv = server_factory("echo.js")
    headers = {"X-H-%d" % i: "v%d" % i for i in range(40)}
    res = harness.request("/", headers=headers)

    assert srv.alive, "server died on 40 request headers:\n%s" % srv.log()
    assert res.status is not None


def test_empty_header_name_is_survivable(server):
    """An empty header field name is accepted and counted by the parser, and
    the runtimes then walk the packed header arrays looking for NUL runs."""
    raw = b"GET / HTTP/1.1\r\nHost: x\r\n: emptyname\r\n\r\n"
    harness.send_raw(raw)
    assert server.alive, (
        "server died on an empty header name -- out-of-bounds scan of the "
        "header array:\n%s" % server.log()
    )
    assert harness.request("/").status == 200


def test_pipelined_requests_in_one_segment(server):
    """Two requests in a single TCP segment.

    The first one completes inside llhttp_execute(), which frees the
    connection from within the parser callback while llhttp still holds a
    pointer to it.
    """
    raw = (
        b"GET / HTTP/1.1\r\nHost: x\r\n\r\n"
        b"GET / HTTP/1.1\r\nHost: x\r\n\r\n"
    )
    harness.send_raw(raw)

    assert server.alive, (
        "process died on pipelined requests -- use-after-free in the parser "
        "path:\n%s" % server.log()
    )

    # The process can survive while the httpd task does not: __httpd_task's
    # error path stops the listener and vTaskDelete()s itself, leaving a live
    # process that accepts nothing.
    try:
        status = harness.request("/").status
    except OSError as exc:
        raise AssertionError(
            "server stopped accepting connections after two pipelined "
            "requests (%r). The process is still alive, so the httpd task "
            "itself is gone -- a remote denial of service.\nserver log:\n%s"
            % (exc, server.log())
        )
    assert status == 200


def test_mixed_hostile_handlers_in_sequence(server_factory, rss_tolerance_kb):
    """Cycle through the awkward handlers; the process must stay healthy."""
    for name in (
        "throws.js",
        "nonstring_header.js",
        "many_headers.js",
        "embedded_nul.js",
        "weird_status.js",
    ):
        srv = server_factory(name)
        _run_many(20)
        assert srv.alive, "server died running %s:\n%s" % (name, srv.log())
        srv.cleanup()
