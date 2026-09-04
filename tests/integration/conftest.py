import json
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(__file__))

from harness import IsereServer, wait_for_port_closed  # noqa: E402

# Areas a test can belong to, in the order they should appear in the report.
AREAS = [
    ("smoke", "Smoke", "Does the server come up and behave like an HTTP server"),
    ("concurrency", "Concurrency & latency", "Multiple clients, latency, fd hygiene"),
    ("memory", "Memory stability", "RSS across sustained traffic and teardown paths"),
    ("risky-js", "Hostile handler code", "What a deployed function can do to the server"),
    ("leaks", "Leak detection (valgrind)", "Memory retained per request, and memory errors"),
]

SEVERITIES = ["critical", "high", "medium", "low", "info"]


def pytest_configure(config):
    config.addinivalue_line(
        "markers",
        "case(id, title, area, severity, proves, refs, known_issue): "
        "descriptive metadata for the test report",
    )
    config.addinivalue_line("markers", "slow: long-running soaks")


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
    parser.addoption(
        "--catalog-out",
        action="store",
        default=None,
        help="Write a JSON catalogue of every collected test (metadata for the "
        "HTML report). Written before -m/-k deselection, so it describes the "
        "whole suite regardless of what this run executes.",
    )


def _case_metadata(item):
    """Pull the @pytest.mark.case metadata off a test, with sane fallbacks."""
    marker = item.get_closest_marker("case")
    kwargs = dict(marker.kwargs) if marker else {}

    doc = ""
    if item.function.__doc__:
        doc = "\n".join(
            line.strip() for line in item.function.__doc__.strip().splitlines()
        ).strip()

    refs = kwargs.get("refs") or []
    if isinstance(refs, str):
        refs = [refs]

    return {
        "nodeid": item.nodeid,
        "name": item.name,
        "module": item.module.__name__ if item.module else "",
        "id": kwargs.get("id", ""),
        "title": kwargs.get("title", item.name.replace("_", " ")),
        "area": kwargs.get("area", ""),
        "severity": kwargs.get("severity", "info"),
        "description": doc,
        "proves": kwargs.get("proves", ""),
        "refs": list(refs),
        "known_issue": kwargs.get("known_issue", ""),
        "slow": item.get_closest_marker("slow") is not None,
    }


@pytest.hookimpl(tryfirst=True)
def pytest_collection_modifyitems(session, config, items):
    """Record every collected test before -m/-k deselection removes any.

    The report is meant to describe the whole suite, including cases this
    particular run did not execute.
    """
    out = config.getoption("--catalog-out")
    if not out:
        return

    catalog = {
        "areas": [
            {"key": key, "title": title, "blurb": blurb}
            for key, title, blurb in AREAS
        ],
        "severities": SEVERITIES,
        "tests": [_case_metadata(item) for item in items],
    }

    path = os.path.abspath(out)
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w") as fh:
        json.dump(catalog, fh, indent=2, sort_keys=True)


@pytest.fixture(autouse=True)
def _record_case_properties(request, record_property):
    """Mirror the metadata into the JUnit XML as testcase properties.

    Keeps the standard report self-describing for consumers that read JUnit
    directly, without depending on the HTML.
    """
    meta = _case_metadata(request.node)
    for key in ("id", "title", "area", "severity", "proves", "known_issue"):
        if meta.get(key):
            record_property(key, meta[key])
    if meta["refs"]:
        record_property("refs", ", ".join(meta["refs"]))
    if meta["description"]:
        record_property("description", meta["description"])


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
