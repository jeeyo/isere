"""Test harness for isère integration tests.

Starts a real `isere` binary in a scratch directory with a test-specific
`handler.so`, then lets tests talk to it over HTTP and watch its memory.

Everything here is Linux-specific (it reads /proc for RSS) and assumes the
`linux` target build, which is what CI builds.
"""

import os
import re
import shutil
import signal
import socket
import subprocess
import tempfile
import time

# ISERE_HTTPD_PORT in include/httpd.h. Not configurable at runtime, so tests
# that need a server must run one at a time.
ISERE_PORT = 8080

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HANDLER_DIR = os.path.join(os.path.dirname(__file__), "handlers")


def isere_binary():
    """Path to the isere executable under test."""
    path = os.environ.get("ISERE_BIN") or os.path.join(REPO_ROOT, "build", "isere")
    if not os.path.isfile(path):
        raise RuntimeError(
            "isere binary not found at %s -- build the linux target first "
            "or set ISERE_BIN" % path
        )
    return path


def build_handler_so(js_path, out_dir):
    """Compile a .js file into the handler.so that isère's loader dlopen()s.

    Mirrors the xxd/sed recipe in the root CMakeLists.txt so that what we test
    is byte-for-byte what a normal build produces.
    """
    js_path = os.path.abspath(js_path)
    if not os.path.isfile(js_path):
        raise RuntimeError("handler js not found: %s" % js_path)

    # xxd derives the symbol name from the input path, so copy to a stable name
    staged_js = os.path.join(out_dir, "handler.js")
    shutil.copyfile(js_path, staged_js)

    handler_c = os.path.join(out_dir, "handler.c")
    with open(handler_c, "w") as fh:
        subprocess.run(
            ["xxd", "-i", "handler.js"], cwd=out_dir, stdout=fh, check=True
        )

    # rename the generated array/length to the symbols loader.c looks up
    with open(handler_c) as fh:
        src = fh.read()
    src = re.sub(r"[a-z_]+\[\]", "handler[]", src)
    src = re.sub(r"[a-z_]+_len", "handler_len", src)
    with open(handler_c, "w") as fh:
        fh.write(src)

    handler_so = os.path.join(out_dir, "handler.so")
    subprocess.run(
        ["cc", "-shared", "-fPIC", "-o", handler_so, handler_c],
        check=True,
        capture_output=True,
    )
    return handler_so


def _port_is_open(port, host="127.0.0.1", timeout=0.2):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def wait_for_port_closed(port=ISERE_PORT, timeout=10.0):
    """Wait for a previous server to fully release the listening port."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not _port_is_open(port):
            return True
        time.sleep(0.05)
    return False


class IsereServer:
    """A running isère process, isolated in its own scratch directory."""

    def __init__(self, handler_js="hello.js", port=ISERE_PORT, valgrind=False):
        if not os.path.isabs(handler_js):
            handler_js = os.path.join(HANDLER_DIR, handler_js)
        self.handler_js = handler_js
        self.port = port
        self.valgrind = valgrind
        self.proc = None
        self.workdir = None
        self._log_file = None

    # -- lifecycle ---------------------------------------------------------

    @property
    def valgrind_log(self):
        if not self.workdir:
            return None
        return os.path.join(self.workdir, "valgrind.log")

    def start(self, ready_timeout=20.0):
        self.workdir = tempfile.mkdtemp(prefix="isere-it-")
        shutil.copy2(isere_binary(), os.path.join(self.workdir, "isere"))
        build_handler_so(self.handler_js, self.workdir)

        env = dict(os.environ)
        # loader.c dlopen()s "handler.so" by relative name, which searches
        # LD_LIBRARY_PATH rather than cwd
        env["LD_LIBRARY_PATH"] = self.workdir

        argv = [os.path.join(self.workdir, "isere")]
        if self.valgrind:
            argv = [
                "valgrind",
                "--leak-check=full",
                "--show-leak-kinds=all",
                # keep ERROR SUMMARY meaning "invalid reads/writes and
                # uninitialised values"; leaks are assessed separately by
                # comparing growth against request count
                "--errors-for-leak-kinds=none",
                "--log-file=%s" % self.valgrind_log,
                "--child-silent-after-fork=yes",
            ] + argv
            ready_timeout = max(ready_timeout, 120.0)

        log_path = os.path.join(self.workdir, "isere.log")
        self._log_file = open(log_path, "w+b")
        self.proc = subprocess.Popen(
            argv,
            cwd=self.workdir,
            env=env,
            stdout=self._log_file,
            stderr=subprocess.STDOUT,
        )

        deadline = time.time() + ready_timeout
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError(
                    "isere exited during startup (rc=%s)\n%s"
                    % (self.proc.returncode, self.log())
                )
            if _port_is_open(self.port):
                return self
            time.sleep(0.05)

        self.stop()
        raise RuntimeError("isere did not start listening\n%s" % self.log())

    def stop(self, timeout=5):
        if self.proc is not None and self.proc.poll() is None:
            if self.valgrind:
                # main.c installs a SIGINT handler that exit()s, which is what
                # makes valgrind emit its leak report. SIGTERM would kill the
                # process before it reports.
                self.proc.send_signal(signal.SIGINT)
                timeout = max(timeout, 120)
            else:
                self.proc.terminate()
            try:
                self.proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=10)
        if self._log_file is not None:
            self._log_file.close()
            self._log_file = None
        wait_for_port_closed(self.port)

    def cleanup(self):
        self.stop()
        if self.workdir and os.path.isdir(self.workdir):
            shutil.rmtree(self.workdir, ignore_errors=True)
            self.workdir = None

    def __enter__(self):
        return self.start()

    def __exit__(self, *exc):
        self.cleanup()
        return False

    # -- introspection -----------------------------------------------------

    @property
    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    @property
    def returncode(self):
        return None if self.proc is None else self.proc.poll()

    def log(self):
        if not self.workdir:
            return ""
        path = os.path.join(self.workdir, "isere.log")
        if not os.path.isfile(path):
            return ""
        with open(path, "rb") as fh:
            return fh.read().decode("utf-8", "replace")

    def rss_kb(self):
        """Resident set size of the server process, in KiB.

        This is the observable we use as a proxy for heap growth: isère is
        built with FreeRTOS heap_3, which is a thin wrapper over libc malloc,
        so leaked allocations show up as process RSS.
        """
        if not self.alive:
            raise RuntimeError("server is not running")
        with open("/proc/%d/status" % self.proc.pid) as fh:
            for line in fh:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
        raise RuntimeError("VmRSS not found in /proc status")

    def open_fd_count(self):
        """Number of open file descriptors -- catches leaked sockets."""
        if not self.alive:
            raise RuntimeError("server is not running")
        return len(os.listdir("/proc/%d/fd" % self.proc.pid))

    def valgrind_report(self):
        """Parse valgrind's leak summary. Call after stop().

        Returns a dict of category -> bytes, plus `errors`.

        Note that most of what isère leaks stays *reachable* -- a leaked
        FreeRTOS timer block is still linked into a kernel list, and a leaked
        atom is still owned by a runtime. So "still reachable" growing with
        request count is the signal, not "definitely lost".
        """
        if not self.valgrind:
            raise RuntimeError("server was not started under valgrind")
        path = self.valgrind_log
        if not path or not os.path.isfile(path):
            raise RuntimeError("no valgrind log at %s" % path)

        with open(path, "r", errors="replace") as fh:
            text = fh.read()

        report = {
            "definitely_lost": 0,
            "indirectly_lost": 0,
            "possibly_lost": 0,
            "still_reachable": 0,
            "errors": 0,
            "raw": text,
        }

        patterns = {
            "definitely_lost": r"definitely lost:\s+([\d,]+) bytes",
            "indirectly_lost": r"indirectly lost:\s+([\d,]+) bytes",
            "possibly_lost": r"possibly lost:\s+([\d,]+) bytes",
            "still_reachable": r"still reachable:\s+([\d,]+) bytes",
        }
        for key, pattern in patterns.items():
            match = re.search(pattern, text)
            if match:
                report[key] = int(match.group(1).replace(",", ""))

        match = re.search(r"ERROR SUMMARY:\s+(\d+) errors", text)
        if match:
            report["errors"] = int(match.group(1))

        return report


# -- minimal HTTP client ---------------------------------------------------
#
# Deliberately hand-rolled rather than using http.client: several tests need to
# send malformed or partial requests, or hang up mid-request, which a
# well-behaved client library will not do.


class HttpResult:
    def __init__(self, status, headers, body, raw):
        self.status = status
        self.headers = headers
        self.body = body
        self.raw = raw

    def __repr__(self):
        return "HttpResult(status=%r, headers=%r, body=%r)" % (
            self.status,
            self.headers,
            self.body[:64],
        )


def _parse_response(raw):
    if not raw:
        return HttpResult(None, {}, b"", raw)

    head, _, body = raw.partition(b"\r\n\r\n")
    lines = head.split(b"\r\n")

    status = None
    if lines and lines[0].startswith(b"HTTP/"):
        parts = lines[0].split(None, 2)
        if len(parts) >= 2:
            try:
                status = int(parts[1])
            except ValueError:
                status = None

    headers = {}
    for line in lines[1:]:
        name, sep, value = line.partition(b":")
        if sep:
            headers[name.strip().decode("latin1").lower()] = (
                value.strip().decode("latin1")
            )

    return HttpResult(status, headers, body, raw)


def request(
    path="/",
    method="GET",
    headers=None,
    body=b"",
    port=ISERE_PORT,
    timeout=10.0,
    host="127.0.0.1",
):
    """Send one request over a fresh connection and read until close.

    isère always replies `Connection: close`, so reading to EOF is the
    correct framing here.
    """
    if isinstance(body, str):
        body = body.encode()

    lines = ["%s %s HTTP/1.1" % (method, path), "Host: %s" % host]
    for name, value in (headers or {}).items():
        lines.append("%s: %s" % (name, value))
    if body:
        lines.append("Content-Length: %d" % len(body))
    raw = ("\r\n".join(lines) + "\r\n\r\n").encode("latin1") + body

    return _parse_response(send_raw(raw, port=port, timeout=timeout, host=host))


def send_raw(payload, port=ISERE_PORT, timeout=10.0, host="127.0.0.1", read=True):
    """Send arbitrary bytes and optionally read the whole reply."""
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall(payload)
        if not read:
            return b""
        chunks = []
        while True:
            try:
                chunk = sock.recv(65536)
            except socket.timeout:
                break
            if not chunk:
                break
            chunks.append(chunk)
        return b"".join(chunks)


def connect_and_close(port=ISERE_PORT, payload=None, timeout=5.0, host="127.0.0.1"):
    """Open a connection, optionally send a partial request, then hang up.

    Exercises the teardown paths that a well-behaved client never reaches.
    """
    with socket.create_connection((host, port), timeout=timeout) as sock:
        if payload:
            sock.sendall(payload)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, b"\1\0\0\0\0\0\0\0")
