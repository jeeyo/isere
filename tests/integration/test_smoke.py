"""Smoke tests: does the server come up and behave like an HTTP server."""

import json

import harness


def test_server_starts_and_listens(server):
    assert server.alive
    assert "Listening on port" in server.log()


def test_get_root_returns_handler_body(server):
    res = harness.request("/")
    assert res.status == 200
    assert json.loads(res.body.decode()) == {
        "ok": True,
        "path": "",
        "method": "GET",
    }


def test_response_carries_server_headers(server):
    res = harness.request("/")
    assert res.headers.get("server") == "isere"
    assert res.headers.get("connection") == "close"
    assert res.headers.get("content-type") == "application/json"


def test_non_root_path_is_404(server):
    # Routing is not implemented: httpd.c only serves the exact root path.
    res = harness.request("/nope")
    assert res.status == 404


def test_post_with_body_reaches_handler(server_factory):
    server_factory("echo.js")
    res = harness.request("/", method="POST", body='{"hello":"world"}')
    assert res.status == 200
    echoed = json.loads(res.body.decode())
    assert echoed["method"] == "POST"
    assert echoed["body"] == {"hello": "world"}


def test_request_headers_reach_handler(server_factory):
    server_factory("echo.js")
    res = harness.request("/", headers={"X-Custom": "abc123"})
    assert res.status == 200
    echoed = json.loads(res.body.decode())
    headers = {k.lower(): v for k, v in echoed["headers"].items()}
    assert headers.get("x-custom") == "abc123"


def test_sequential_requests_all_succeed(server):
    for i in range(25):
        res = harness.request("/")
        assert res.status == 200, "request %d failed: %r" % (i, res)


def test_server_survives_a_throwing_handler(server_factory):
    srv = server_factory("throws.js")
    for _ in range(5):
        harness.request("/")
    assert srv.alive, "server died on a throwing handler:\n%s" % srv.log()
    # and it still serves afterwards
    assert harness.request("/").status is not None


def test_server_survives_malformed_request(server):
    harness.send_raw(b"NOT-HTTP\r\n\r\n")
    assert server.alive
    assert harness.request("/").status == 200


def test_server_survives_immediate_disconnect(server):
    for _ in range(20):
        harness.connect_and_close()
    assert server.alive
    assert harness.request("/").status == 200


def test_server_survives_partial_request_then_disconnect(server):
    for _ in range(20):
        harness.connect_and_close(payload=b"GET / HTTP/1.1\r\nHost: x\r\n")
    assert server.alive
    assert harness.request("/").status == 200
