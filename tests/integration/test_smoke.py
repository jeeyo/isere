"""Smoke tests: does the server come up and behave like an HTTP server."""

import json

import pytest

import harness

AREA = "smoke"


@pytest.mark.case(
    id="SMOKE-01",
    title="Server starts and listens",
    area=AREA,
    severity="critical",
    proves="The binary cannot serve at all -- every other result is meaningless.",
    refs=["src/main.c:126", "src/httpd.c:390"],
)
def test_server_starts_and_listens(server):
    """Start the binary with a compiled handler.so and wait for it to bind
    port 8080, then confirm it logged that it is listening."""
    assert server.alive
    assert "Listening on port" in server.log()


@pytest.mark.case(
    id="SMOKE-02",
    title="GET / runs the handler and returns its body",
    area=AREA,
    severity="critical",
    proves="The JavaScript handler is not being invoked, or its return value "
    "is not reaching the response.",
    refs=["src/httpd.c:213", "src/runtimes/quickjs/quickjs.c:119"],
)
def test_get_root_returns_handler_body(server):
    """Issue GET / and check the JSON body matches exactly what hello.js
    returns, including the event fields the runtime populates."""
    res = harness.request("/")
    assert res.status == 200
    assert json.loads(res.body.decode()) == {
        "ok": True,
        "path": "",
        "method": "GET",
    }


@pytest.mark.case(
    id="SMOKE-03",
    title="Response carries the expected headers",
    area=AREA,
    severity="low",
    proves="Header writeback is dropping or mangling headers.",
    refs=["src/httpd.c:497"],
)
def test_response_carries_server_headers(server):
    """Check the handler-supplied Content-Type survives alongside the Server
    and Connection headers the C side appends."""
    res = harness.request("/")
    assert res.headers.get("server") == "isere"
    assert res.headers.get("connection") == "close"
    assert res.headers.get("content-type") == "application/json"


@pytest.mark.case(
    id="SMOKE-04",
    title="Non-root paths return 404",
    area=AREA,
    severity="info",
    proves="Routing behaviour changed. Records current behaviour rather than "
    "endorsing it.",
    refs=["src/httpd.c:191"],
    known_issue="Routing is not implemented; only the exact root path is "
    "served. The check reads oddly because yuarel_parse() NUL-terminates "
    "conn->path in place before it runs.",
)
def test_non_root_path_is_404(server):
    """Request /nope and confirm it is refused rather than dispatched."""
    res = harness.request("/nope")
    assert res.status == 404


@pytest.mark.case(
    id="SMOKE-05",
    title="POST body is parsed and reaches the handler",
    area=AREA,
    severity="medium",
    proves="Request bodies are not being parsed, or JSON parsing of the body "
    "is broken.",
    refs=["src/httpd.c:161", "src/runtimes/quickjs/quickjs.c:161"],
)
def test_post_with_body_reaches_handler(server_factory):
    """POST a JSON document and have the echo handler return what it saw, so
    both the body plumbing and the JSON parse are checked."""
    server_factory("echo.js")
    res = harness.request("/", method="POST", body='{"hello":"world"}')
    assert res.status == 200
    echoed = json.loads(res.body.decode())
    assert echoed["method"] == "POST"
    assert echoed["body"] == {"hello": "world"}


@pytest.mark.case(
    id="SMOKE-06",
    title="Request headers reach the handler",
    area=AREA,
    severity="medium",
    proves="The header array is not being handed to the runtime correctly.",
    refs=["src/http_handler.c:20", "src/runtimes/quickjs/quickjs.c:139"],
)
def test_request_headers_reach_handler(server_factory):
    """Send a custom request header and confirm the handler sees it in
    event.headers."""
    server_factory("echo.js")
    res = harness.request("/", headers={"X-Custom": "abc123"})
    assert res.status == 200
    echoed = json.loads(res.body.decode())
    headers = {k.lower(): v for k, v in echoed["headers"].items()}
    assert headers.get("x-custom") == "abc123"


@pytest.mark.case(
    id="SMOKE-07",
    title="Sequential requests all succeed",
    area=AREA,
    severity="high",
    proves="State is leaking between connections, or the per-request runtime "
    "is not being reset cleanly.",
    refs=["src/httpd.c:367", "src/runtimes/quickjs/quickjs.c:358"],
)
def test_sequential_requests_all_succeed(server):
    """Issue 25 requests back to back on fresh connections; every one must
    return 200."""
    for i in range(25):
        res = harness.request("/")
        assert res.status == 200, "request %d failed: %r" % (i, res)


@pytest.mark.case(
    id="SMOKE-08",
    title="A throwing handler does not take the server down",
    area=AREA,
    severity="high",
    proves="An exception in user JavaScript can kill the whole platform.",
    refs=["src/runtimes/quickjs/quickjs.c:190"],
)
def test_server_survives_a_throwing_handler(server_factory):
    """Deploy a handler that throws, hit it repeatedly, and confirm the
    process is still alive and still serving."""
    srv = server_factory("throws.js")
    for _ in range(5):
        harness.request("/")
    assert srv.alive, "server died on a throwing handler:\n%s" % srv.log()
    assert harness.request("/").status is not None


@pytest.mark.case(
    id="SMOKE-09",
    title="Malformed request is rejected cleanly",
    area=AREA,
    severity="high",
    proves="Garbage on the socket can crash the parser.",
    refs=["src/httpd.c:252"],
)
def test_server_survives_malformed_request(server):
    """Send bytes that are not HTTP at all, then confirm the next well-formed
    request still works."""
    harness.send_raw(b"NOT-HTTP\r\n\r\n")
    assert server.alive
    assert harness.request("/").status == 200


@pytest.mark.case(
    id="SMOKE-10",
    title="Immediate disconnect is handled",
    area=AREA,
    severity="high",
    proves="Connections dropped before any data arrives are not cleaned up "
    "safely.",
    refs=["src/httpd.c:232", "src/httpd.c:367"],
)
def test_server_survives_immediate_disconnect(server):
    """Open and immediately close 20 connections with SO_LINGER set so they
    RST, then confirm the server still serves."""
    for _ in range(20):
        harness.connect_and_close()
    assert server.alive
    assert harness.request("/").status == 200


@pytest.mark.case(
    id="SMOKE-11",
    title="Disconnect mid-request is handled",
    area=AREA,
    severity="high",
    proves="A connection abandoned part-way through parsing leaves the "
    "parser or the connection object in a bad state.",
    refs=["src/httpd.c:219", "src/httpd.c:367"],
)
def test_server_survives_partial_request_then_disconnect(server):
    """Send a half-finished request head and hang up, 20 times over."""
    for _ in range(20):
        harness.connect_and_close(payload=b"GET / HTTP/1.1\r\nHost: x\r\n")
    assert server.alive
    assert harness.request("/").status == 200
