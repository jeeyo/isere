import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(__file__))

from harness import IsereServer, wait_for_port_closed  # noqa: E402


def pytest_addoption(parser):
    parser.addoption(
        "--rss-tolerance-kb",
        action="store",
        type=int,
        default=512,
        help="RSS growth (KiB) tolerated across a soak before it counts as a leak",
    )
    parser.addoption(
        "--soak-requests",
        action="store",
        type=int,
        default=200,
        help="Number of requests used by the memory soak tests",
    )


@pytest.fixture(scope="session", autouse=True)
def _require_linux():
    if not sys.platform.startswith("linux"):
        pytest.skip(
            "integration tests read /proc and target the linux build; "
            "run them in the container"
        )


@pytest.fixture(autouse=True)
def _port_is_free():
    # isère's listen port is a compile-time constant, so tests must not overlap
    wait_for_port_closed()
    yield
    wait_for_port_closed()


@pytest.fixture
def server_factory():
    """Start an isère server with a chosen handler; cleaned up automatically."""
    started = []

    def _factory(handler_js="hello.js"):
        srv = IsereServer(handler_js=handler_js)
        srv.start()
        started.append(srv)
        return srv

    yield _factory

    for srv in started:
        srv.cleanup()


@pytest.fixture
def server(server_factory):
    """A server running the plain `hello.js` handler."""
    return server_factory("hello.js")


@pytest.fixture
def rss_tolerance_kb(request):
    return request.config.getoption("--rss-tolerance-kb")


@pytest.fixture
def soak_requests(request):
    return request.config.getoption("--soak-requests")
