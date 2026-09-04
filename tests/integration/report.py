#!/usr/bin/env python3
"""Build a self-contained HTML report from JUnit XML plus the test catalogue.

The JUnit XML stays a standard artifact that any CI consumer can read. This
merges it with the catalogue conftest.py writes at collection time, so the
HTML describes *every* test in the suite -- including ones the run did not
execute -- with its purpose, what a failure would mean, and the source it
exercises.

Usage:
    python3 report.py --catalog catalog.json \
                      --junit results.xml [--junit more.xml ...] \
                      --out report.html
"""

import argparse
import datetime
import html
import json
import os
import platform
import xml.etree.ElementTree as ET

STATUS_ORDER = {"failed": 0, "error": 0, "skipped": 1, "passed": 2, "not run": 3}

SEVERITY_BLURB = {
    "critical": "Memory corruption or data disclosure reachable from the network",
    "high": "Crash, hang, or unbounded growth reachable from a request",
    "medium": "Incorrect behaviour or resource growth under load",
    "low": "Robustness and boundary handling",
    "info": "Characterisation -- records behaviour rather than asserting a bug",
}


def parse_junit(paths):
    """Return {nodeid: result} merged across one or more JUnit XML files."""
    results = {}
    suites = []

    for path in paths:
        if not os.path.isfile(path):
            continue
        root = ET.parse(path).getroot()
        found = (
            root.findall(".//testsuite") if root.tag == "testsuites" else [root]
        )
        for suite in found:
            suites.append(
                {
                    "name": suite.get("name", ""),
                    "time": float(suite.get("time") or 0.0),
                    "timestamp": suite.get("timestamp", ""),
                    "hostname": suite.get("hostname", ""),
                }
            )
            for case in suite.findall("testcase"):
                nodeid = _nodeid(case)

                status = "passed"
                message = ""
                detail = ""
                for kind in ("failure", "error"):
                    node = case.find(kind)
                    if node is not None:
                        status = "failed"
                        message = node.get("message", "") or ""
                        detail = node.text or ""
                        break
                else:
                    node = case.find("skipped")
                    if node is not None:
                        status = "skipped"
                        message = node.get("message", "") or ""
                        detail = node.text or ""

                props = {}
                for prop in case.findall("./properties/property"):
                    name = prop.get("name")
                    if name:
                        props[name] = prop.get("value", "")

                results[nodeid] = {
                    "status": status,
                    "message": message,
                    "detail": detail,
                    "time": float(case.get("time") or 0.0),
                    "properties": props,
                    "stdout": _text(case, "system-out"),
                    "stderr": _text(case, "system-err"),
                }

    return results, suites


def _nodeid(case):
    """Reconstruct the pytest nodeid from a JUnit testcase element."""
    file_attr = case.get("file")
    name = case.get("name", "")
    if file_attr:
        return "%s::%s" % (file_attr, name)

    classname = case.get("classname", "")
    if classname:
        return "%s::%s" % (classname.replace(".", "/") + ".py", name)
    return name


def _text(case, tag):
    node = case.find(tag)
    if node is None:
        return ""
    return _strip_capture_banners(node.text or "")


def _strip_capture_banners(text):
    """Drop pytest's `----- Captured Xxx -----` rules and empty sections.

    pytest emits a banner per capture channel whether or not anything was
    captured, which leaves the report full of empty blocks.
    """
    sections = []
    current = []
    for line in text.splitlines():
        if line.startswith("-") and line.rstrip().endswith("-") and "Captured" in line:
            sections.append(current)
            current = []
            continue
        current.append(line)
    sections.append(current)

    kept = []
    for section in sections:
        body = "\n".join(section).strip()
        if body:
            kept.append(body)
    return "\n\n".join(kept).strip()


def _match(catalog_tests, results):
    """Attach a result to each catalogued test.

    JUnit encodes the path with the rootdir stripped, so fall back to matching
    on the trailing file::name when the full nodeid does not line up.
    """
    by_suffix = {}
    for nodeid, res in results.items():
        by_suffix.setdefault(nodeid.split("/")[-1], res)

    merged = []
    for test in catalog_tests:
        nodeid = test["nodeid"]
        res = results.get(nodeid) or by_suffix.get(nodeid.split("/")[-1])
        entry = dict(test)
        if res:
            entry.update(
                {
                    "status": res["status"],
                    "message": res["message"],
                    "detail": res["detail"],
                    "time": res["time"],
                    "stdout": res["stdout"],
                    "stderr": res["stderr"],
                }
            )
        else:
            entry.update(
                {
                    "status": "not run",
                    "message": "",
                    "detail": "",
                    "time": 0.0,
                    "stdout": "",
                    "stderr": "",
                }
            )
        merged.append(entry)
    return merged


def _esc(text):
    return html.escape(text or "", quote=True)


def _block(label, body):
    if not (body or "").strip():
        return ""
    return (
        '<div class="block"><div class="block-label">%s</div>'
        '<pre>%s</pre></div>' % (_esc(label), _esc(body.strip()))
    )


def render(catalog, tests, suites, title):
    counts = {"passed": 0, "failed": 0, "skipped": 0, "not run": 0}
    for test in tests:
        counts[test["status"]] = counts.get(test["status"], 0) + 1

    total = len(tests)
    duration = sum(s["time"] for s in suites)
    executed = total - counts["not run"]

    failing_by_severity = {}
    for test in tests:
        if test["status"] == "failed":
            failing_by_severity.setdefault(test["severity"], []).append(test)

    areas = catalog.get("areas") or []
    known_areas = {a["key"] for a in areas}
    extra = sorted({t["area"] for t in tests if t["area"] not in known_areas})
    areas = areas + [{"key": k, "title": k or "Other", "blurb": ""} for k in extra]

    generated = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    parts = [_HEAD % {"title": _esc(title)}]
    parts.append('<header class="page-head">')
    parts.append("<h1>%s</h1>" % _esc(title))
    parts.append(
        '<p class="sub">%d tests &middot; %d executed &middot; %.1fs '
        "&middot; generated %s on %s</p>"
        % (total, executed, duration, _esc(generated), _esc(platform.node()))
    )
    parts.append("</header>")

    # summary cards
    parts.append('<section class="cards">')
    for key, label in (
        ("passed", "Passed"),
        ("failed", "Failed"),
        ("skipped", "Skipped"),
        ("not run", "Not run"),
    ):
        parts.append(
            '<div class="card %s"><div class="num">%d</div>'
            '<div class="lbl">%s</div></div>'
            % (key.replace(" ", "-"), counts.get(key, 0), label)
        )
    parts.append("</section>")

    # failures up front, ordered by severity
    if counts["failed"]:
        parts.append('<section class="findings">')
        parts.append("<h2>Failures</h2>")
        parts.append(
            "<p class=\"note\">Each of these is a finding about the server, "
            "not a broken test. Severity reflects what the failure implies.</p>"
        )
        for severity in catalog.get("severities", []):
            for test in failing_by_severity.get(severity, []):
                parts.append(
                    '<div class="finding sev-%s">'
                    '<a href="#%s"><span class="sev">%s</span>'
                    '<span class="tid">%s</span>'
                    '<span class="ttl">%s</span></a>'
                    '<div class="why">%s</div></div>'
                    % (
                        _esc(severity),
                        _esc(test["nodeid"]),
                        _esc(severity),
                        _esc(test["id"]),
                        _esc(test["title"]),
                        _esc(test["proves"] or test["description"]),
                    )
                )
        parts.append("</section>")

    # filter controls
    parts.append(
        '<div class="filters">'
        '<button class="on" data-filter="all">All</button>'
        '<button data-filter="failed">Failed</button>'
        '<button data-filter="passed">Passed</button>'
        '<button data-filter="skipped">Skipped</button>'
        '<button data-filter="not-run">Not run</button>'
        "</div>"
    )

    # the catalogue itself
    for area in areas:
        in_area = [t for t in tests if t["area"] == area["key"]]
        if not in_area:
            continue
        in_area.sort(key=lambda t: (STATUS_ORDER.get(t["status"], 9), t["id"]))

        parts.append('<section class="area">')
        parts.append(
            '<h2>%s <span class="count">%d</span></h2>'
            % (_esc(area["title"]), len(in_area))
        )
        if area.get("blurb"):
            parts.append('<p class="blurb">%s</p>' % _esc(area["blurb"]))

        for test in in_area:
            status_class = test["status"].replace(" ", "-")
            parts.append(
                '<article class="test %s" id="%s" data-status="%s">'
                % (status_class, _esc(test["nodeid"]), status_class)
            )
            parts.append('<div class="test-head">')
            parts.append(
                '<span class="status %s">%s</span>'
                % (status_class, _esc(test["status"]))
            )
            parts.append('<span class="tid">%s</span>' % _esc(test["id"]))
            parts.append('<h3>%s</h3>' % _esc(test["title"]))
            parts.append(
                '<span class="sev sev-%s" title="%s">%s</span>'
                % (
                    _esc(test["severity"]),
                    _esc(SEVERITY_BLURB.get(test["severity"], "")),
                    _esc(test["severity"]),
                )
            )
            if test["slow"]:
                parts.append('<span class="tag">slow</span>')
            if test["time"]:
                parts.append('<span class="dur">%.2fs</span>' % test["time"])
            parts.append("</div>")

            if test["description"]:
                parts.append(
                    '<p class="desc">%s</p>' % _esc(test["description"])
                )
            if test["proves"]:
                parts.append(
                    '<p class="proves"><b>If this fails:</b> %s</p>'
                    % _esc(test["proves"])
                )
            if test["known_issue"]:
                parts.append(
                    '<p class="known"><b>Known issue:</b> %s</p>'
                    % _esc(test["known_issue"])
                )
            if test["refs"]:
                parts.append(
                    '<p class="refs"><b>Exercises:</b> %s</p>'
                    % " ".join(
                        '<code>%s</code>' % _esc(r) for r in test["refs"]
                    )
                )
            parts.append(
                '<p class="nodeid"><code>%s</code></p>' % _esc(test["nodeid"])
            )

            if test["status"] == "failed":
                parts.append(_block("Failure", test["message"]))
                parts.append(_block("Traceback", test["detail"]))
            elif test["status"] == "skipped" and test["message"]:
                parts.append(_block("Skipped", test["message"]))
            parts.append(_block("Captured output", test["stdout"]))
            parts.append(_block("Captured stderr", test["stderr"]))
            parts.append("</article>")
        parts.append("</section>")

    parts.append(_FOOT)
    return "\n".join(parts)


_HEAD = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%(title)s</title>
<style>
:root{
  --bg:#f6f7f9; --panel:#fff; --ink:#14171a; --muted:#5c6670; --line:#dfe3e8;
  --pass:#1a7f45; --fail:#c0322b; --skip:#8a6d1f; --none:#6b7280;
  --pass-bg:#e8f5ee; --fail-bg:#fdecea; --skip-bg:#fdf6e3; --none-bg:#f1f2f4;
  --accent:#2b5fa8;
}
@media (prefers-color-scheme: dark){
  :root:not([data-theme="light"]){
    --bg:#14171a; --panel:#1c2023; --ink:#e8eaed; --muted:#9aa4ae; --line:#2c3237;
    --pass:#5fd08a; --fail:#ff8a80; --skip:#e8c463; --none:#9aa4ae;
    --pass-bg:#16301f; --fail-bg:#3a1c1a; --skip-bg:#332b14; --none-bg:#22262a;
    --accent:#7fa9e8;
  }
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);
  font:14px/1.55 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
  padding:24px;max-width:1100px;margin:0 auto}
h1{font-size:22px;margin:0 0 4px}
h2{font-size:17px;margin:28px 0 6px;padding-bottom:6px;border-bottom:1px solid var(--line)}
h3{font-size:14px;margin:0;font-weight:600}
.page-head .sub{color:var(--muted);margin:0 0 18px}
.cards{display:flex;gap:10px;flex-wrap:wrap;margin-bottom:8px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:8px;
  padding:12px 18px;min-width:110px}
.card .num{font-size:24px;font-weight:650;line-height:1.1}
.card .lbl{color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.04em}
.card.passed .num{color:var(--pass)} .card.failed .num{color:var(--fail)}
.card.skipped .num{color:var(--skip)} .card.not-run .num{color:var(--none)}
.note,.blurb{color:var(--muted);margin:4px 0 12px}
.findings{margin-top:22px}
.finding{background:var(--panel);border:1px solid var(--line);border-left:4px solid var(--fail);
  border-radius:6px;padding:10px 14px;margin-bottom:8px}
.finding a{text-decoration:none;color:var(--ink);display:flex;gap:10px;align-items:center;flex-wrap:wrap}
.finding .why{color:var(--muted);margin-top:4px;font-size:13px}
.filters{display:flex;gap:6px;margin:20px 0 4px;flex-wrap:wrap}
.filters button{background:var(--panel);border:1px solid var(--line);color:var(--muted);
  border-radius:999px;padding:5px 14px;cursor:pointer;font-size:13px}
.filters button.on{background:var(--accent);border-color:var(--accent);color:#fff}
.count{color:var(--muted);font-weight:400;font-size:13px}
.test{background:var(--panel);border:1px solid var(--line);border-radius:8px;
  padding:12px 14px;margin-bottom:10px}
.test.failed{border-left:4px solid var(--fail)}
.test.passed{border-left:4px solid var(--pass)}
.test.skipped{border-left:4px solid var(--skip)}
.test.not-run{border-left:4px solid var(--none);opacity:.85}
.test-head{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
.status{font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.05em;
  padding:2px 8px;border-radius:4px}
.status.passed{background:var(--pass-bg);color:var(--pass)}
.status.failed{background:var(--fail-bg);color:var(--fail)}
.status.skipped{background:var(--skip-bg);color:var(--skip)}
.status.not-run{background:var(--none-bg);color:var(--none)}
.tid{font:600 12px/1 ui-monospace,SFMono-Regular,Menlo,monospace;color:var(--accent)}
.sev{font-size:11px;text-transform:uppercase;letter-spacing:.04em;color:var(--muted);
  border:1px solid var(--line);border-radius:4px;padding:1px 7px}
.sev-critical{color:var(--fail);border-color:var(--fail)}
.sev-high{color:var(--fail)}
.tag{font-size:11px;color:var(--muted);border:1px dashed var(--line);border-radius:4px;padding:1px 7px}
.dur{margin-left:auto;color:var(--muted);font-size:12px}
.desc{margin:8px 0 0}
.proves,.known,.refs,.nodeid{margin:5px 0 0;color:var(--muted);font-size:13px}
.known{color:var(--skip)}
code{font:12px/1.4 ui-monospace,SFMono-Regular,Menlo,monospace;
  background:var(--none-bg);padding:1px 5px;border-radius:3px}
.block{margin-top:10px}
.block-label{font-size:11px;text-transform:uppercase;letter-spacing:.05em;
  color:var(--muted);margin-bottom:3px}
pre{margin:0;background:var(--none-bg);border:1px solid var(--line);border-radius:6px;
  padding:10px;overflow-x:auto;font:12px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace;
  white-space:pre-wrap;word-break:break-word;max-height:420px}
</style></head><body>
"""

_FOOT = """
<script>
document.querySelectorAll('.filters button').forEach(function (btn) {
  btn.addEventListener('click', function () {
    var want = btn.dataset.filter;
    document.querySelectorAll('.filters button').forEach(function (b) {
      b.classList.toggle('on', b === btn);
    });
    document.querySelectorAll('article.test').forEach(function (el) {
      el.hidden = !(want === 'all' || el.dataset.status === want);
    });
    document.querySelectorAll('section.area').forEach(function (sec) {
      var any = sec.querySelector('article.test:not([hidden])');
      sec.hidden = !any;
    });
  });
});
</script>
</body></html>
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--catalog", required=True, help="catalog.json from conftest")
    ap.add_argument(
        "--junit", action="append", default=[], help="JUnit XML (repeatable)"
    )
    ap.add_argument("--out", required=True, help="HTML file to write")
    ap.add_argument("--title", default="isère integration tests")
    args = ap.parse_args()

    with open(args.catalog) as fh:
        catalog = json.load(fh)

    results, suites = parse_junit(args.junit)
    tests = _match(catalog.get("tests", []), results)

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w") as fh:
        fh.write(render(catalog, tests, suites, args.title))

    counts = {}
    for test in tests:
        counts[test["status"]] = counts.get(test["status"], 0) + 1
    print(
        "wrote %s (%d tests: %s)"
        % (
            out,
            len(tests),
            ", ".join("%d %s" % (v, k) for k, v in sorted(counts.items())),
        )
    )


if __name__ == "__main__":
    main()
