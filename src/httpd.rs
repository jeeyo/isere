// HTTP/1.1 server for isere.
//
// Uses httparse for zero-copy HTTP parsing (no_std compatible).
// Non-blocking sockets with poll()-based event loop.

use core::ffi::c_int;

#[cfg(not(test))]
use crate::event_loop::EventLoop;
#[cfg(not(test))]
use crate::platform::tcp;

pub const HTTPD_PORT: u16 = 8080;
pub const MAX_CONNECTIONS: usize = 12;
pub const MAX_HEADERS: usize = 16;
pub const MAX_REQUEST_SIZE: usize = 2048;
pub const MAX_RESPONSE_BODY_LEN: usize = 4096;
pub const MAX_RESPONSE_HEADER_NAME_LEN: usize = 64;
pub const MAX_RESPONSE_HEADER_VALUE_LEN: usize = 256;

/// HTTP method.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Method {
    Get,
    Post,
    Put,
    Delete,
    Patch,
    Options,
    Head,
    Unknown,
}

impl Method {
    pub fn from_bytes(s: &[u8]) -> Self {
        match s {
            b"GET" => Method::Get,
            b"POST" => Method::Post,
            b"PUT" => Method::Put,
            b"DELETE" => Method::Delete,
            b"PATCH" => Method::Patch,
            b"OPTIONS" => Method::Options,
            b"HEAD" => Method::Head,
            _ => Method::Unknown,
        }
    }

    pub fn as_str(&self) -> &'static str {
        match self {
            Method::Get => "GET",
            Method::Post => "POST",
            Method::Put => "PUT",
            Method::Delete => "DELETE",
            Method::Patch => "PATCH",
            Method::Options => "OPTIONS",
            Method::Head => "HEAD",
            Method::Unknown => "UNKNOWN",
        }
    }
}

/// A zero-copy parsed HTTP request. All fields borrow from the receive buffer.
pub struct HttpRequest<'a> {
    pub method: Method,
    pub path: &'a str,
    pub query: &'a str,
    pub body: &'a [u8],
    headers_buf: [httparse::Header<'a>; MAX_HEADERS],
    pub num_headers: usize,
}

impl<'a> HttpRequest<'a> {
    pub fn headers(&self) -> &[httparse::Header<'a>] {
        &self.headers_buf[..self.num_headers]
    }

    pub fn method_str(&self) -> &str {
        self.method.as_str()
    }

    pub fn body_str(&self) -> &str {
        core::str::from_utf8(self.body).unwrap_or("")
    }
}

/// Parse an HTTP request from a buffer using httparse (zero-copy).
///
/// Returns the parsed request and the body start offset on success.
/// All fields in the returned HttpRequest borrow directly from `buf`.
pub fn parse_request(buf: &[u8]) -> Result<(HttpRequest<'_>, usize), ()> {
    let mut headers = [httparse::EMPTY_HEADER; MAX_HEADERS];
    let mut parsed = httparse::Request::new(&mut headers);

    let body_start = match parsed.parse(buf) {
        Ok(httparse::Status::Complete(n)) => n,
        Ok(httparse::Status::Partial) => return Err(()),
        Err(_) => return Err(()),
    };

    let method = Method::from_bytes(parsed.method.unwrap_or("").as_bytes());

    let full_path = parsed.path.unwrap_or("/");
    let (path, query) = match full_path.find('?') {
        Some(pos) => (&full_path[..pos], &full_path[pos + 1..]),
        None => (full_path, ""),
    };

    let num_headers = parsed.headers.len();
    let body = &buf[body_start..];

    drop(parsed);

    Ok((
        HttpRequest {
            method,
            path,
            query,
            body,
            headers_buf: headers,
            num_headers,
        },
        body_start,
    ))
}

/// Check if headers are complete and body is fully received.
/// Used during the reading phase to decide when to stop reading.
fn is_request_complete(buf: &[u8], buf_len: usize) -> bool {
    let mut headers = [httparse::EMPTY_HEADER; MAX_HEADERS];
    let mut parsed = httparse::Request::new(&mut headers);

    let body_start = match parsed.parse(buf) {
        Ok(httparse::Status::Complete(n)) => n,
        _ => return false,
    };

    let content_length = get_content_length(parsed.headers);
    if content_length > 0 {
        let available = buf_len - body_start;
        return available >= content_length;
    }

    true
}

/// Get Content-Length from httparse headers.
fn get_content_length(headers: &[httparse::Header<'_>]) -> usize {
    for h in headers {
        if h.name.eq_ignore_ascii_case("content-length") {
            if let Ok(s) = core::str::from_utf8(h.value) {
                if let Ok(n) = s.trim().parse::<usize>() {
                    return n;
                }
            }
        }
    }
    0
}

/// An HTTP response header.
pub struct ResponseHeader {
    pub name: [u8; MAX_RESPONSE_HEADER_NAME_LEN],
    pub name_len: usize,
    pub value: [u8; MAX_RESPONSE_HEADER_VALUE_LEN],
    pub value_len: usize,
}

impl ResponseHeader {
    pub const fn empty() -> Self {
        Self {
            name: [0u8; MAX_RESPONSE_HEADER_NAME_LEN],
            name_len: 0,
            value: [0u8; MAX_RESPONSE_HEADER_VALUE_LEN],
            value_len: 0,
        }
    }
}

/// An HTTP response to be sent back.
pub struct HttpResponse {
    pub status_code: u16,
    pub headers: [ResponseHeader; MAX_HEADERS],
    pub num_headers: usize,
    pub body: [u8; MAX_RESPONSE_BODY_LEN],
    pub body_len: usize,
    pub completed: bool,
}

impl HttpResponse {
    pub const fn new() -> Self {
        Self {
            status_code: 200,
            headers: [const { ResponseHeader::empty() }; MAX_HEADERS],
            num_headers: 0,
            body: [0u8; MAX_RESPONSE_BODY_LEN],
            body_len: 0,
            completed: false,
        }
    }

    pub fn set_header(&mut self, name: &str, value: &str) {
        if self.num_headers >= MAX_HEADERS {
            return;
        }
        let h = &mut self.headers[self.num_headers];
        let n = name.len().min(MAX_RESPONSE_HEADER_NAME_LEN);
        h.name[..n].copy_from_slice(&name.as_bytes()[..n]);
        h.name_len = n;
        let v = value.len().min(MAX_RESPONSE_HEADER_VALUE_LEN);
        h.value[..v].copy_from_slice(&value.as_bytes()[..v]);
        h.value_len = v;
        self.num_headers += 1;
    }

    pub fn set_body(&mut self, body: &[u8]) {
        let n = body.len().min(MAX_RESPONSE_BODY_LEN);
        self.body[..n].copy_from_slice(&body[..n]);
        self.body_len = n;
    }
}

#[cfg(not(test))]
/// State of an HTTP connection.
#[derive(Clone, Copy, PartialEq)]
pub enum ConnState {
    Free,
    Reading,
    Processing,
    Writing,
}

#[cfg(not(test))]
/// A connection slot in the server.
pub struct Connection {
    pub fd: c_int,
    pub state: ConnState,
    pub watcher_id: usize,

    // receive buffer for incremental parsing
    pub recv_buf: [u8; MAX_REQUEST_SIZE],
    pub recv_len: usize,

    pub parse_complete: bool,

    // response
    pub response: HttpResponse,

    // JS context index (Phase 5: used to track JS execution)
    pub js_context_id: i32,
}

#[cfg(not(test))]
impl Connection {
    pub const fn empty() -> Self {
        Self {
            fd: -1,
            state: ConnState::Free,
            watcher_id: 0,
            recv_buf: [0u8; MAX_REQUEST_SIZE],
            recv_len: 0,
            parse_complete: false,
            response: HttpResponse::new(),
            js_context_id: -1,
        }
    }

    pub fn reset(&mut self) {
        self.fd = -1;
        self.state = ConnState::Free;
        self.watcher_id = 0;
        self.recv_buf = [0u8; MAX_REQUEST_SIZE];
        self.recv_len = 0;
        self.parse_complete = false;
        self.response = HttpResponse::new();
        self.js_context_id = -1;
    }
}

#[cfg(not(test))]
/// Write an HTTP response to a socket.
pub fn write_response(fd: c_int, response: &HttpResponse) {
    // Status line
    let _ = tcp::send_all(fd, b"HTTP/1.1 ");

    let mut status_buf = [0u8; 16];
    let status_len = write_u16(response.status_code, &mut status_buf);
    let _ = tcp::send_all(fd, &status_buf[..status_len]);
    let _ = tcp::send_all(fd, b" ");
    let _ = tcp::send_all(fd, status_text(response.status_code).as_bytes());
    let _ = tcp::send_all(fd, b"\r\n");

    // Response headers from handler
    for i in 0..response.num_headers {
        let h = &response.headers[i];
        let _ = tcp::send_all(fd, &h.name[..h.name_len]);
        let _ = tcp::send_all(fd, b": ");
        let _ = tcp::send_all(fd, &h.value[..h.value_len]);
        let _ = tcp::send_all(fd, b"\r\n");
    }

    // Server headers
    let _ = tcp::send_all(fd, b"Server: isere\r\n");
    let _ = tcp::send_all(fd, b"Connection: close\r\n");

    // Content-Length
    let _ = tcp::send_all(fd, b"Content-Length: ");
    let mut len_buf = [0u8; 16];
    let len_len = write_usize(response.body_len, &mut len_buf);
    let _ = tcp::send_all(fd, &len_buf[..len_len]);
    let _ = tcp::send_all(fd, b"\r\n");

    // End of headers
    let _ = tcp::send_all(fd, b"\r\n");

    // Body
    if response.body_len > 0 {
        let _ = tcp::send_all(fd, &response.body[..response.body_len]);
    }
}

#[cfg(not(test))]
fn status_text(code: u16) -> &'static str {
    match code {
        200 => "OK",
        201 => "Created",
        204 => "No Content",
        301 => "Moved Permanently",
        302 => "Found",
        304 => "Not Modified",
        400 => "Bad Request",
        401 => "Unauthorized",
        403 => "Forbidden",
        404 => "Not Found",
        405 => "Method Not Allowed",
        413 => "Content Too Large",
        500 => "Internal Server Error",
        502 => "Bad Gateway",
        503 => "Service Unavailable",
        _ => "OK",
    }
}

#[cfg(not(test))]
/// Write a u16 to a buffer as ASCII decimal. Returns number of bytes written.
fn write_u16(val: u16, buf: &mut [u8]) -> usize {
    write_usize(val as usize, buf)
}

#[cfg(not(test))]
/// Write a usize to a buffer as ASCII decimal. Returns number of bytes written.
fn write_usize(mut val: usize, buf: &mut [u8]) -> usize {
    if val == 0 {
        buf[0] = b'0';
        return 1;
    }
    let mut tmp = [0u8; 20];
    let mut i = 0;
    while val > 0 {
        tmp[i] = b'0' + (val % 10) as u8;
        val /= 10;
        i += 1;
    }
    for j in 0..i {
        buf[j] = tmp[i - 1 - j];
    }
    i
}

#[cfg(not(test))]
/// The HTTP server.
pub struct HttpServer {
    pub server_fd: c_int,
    pub server_watcher_id: usize,
    pub connections: [Connection; MAX_CONNECTIONS],
    pub should_exit: bool,
}

#[cfg(not(test))]
impl HttpServer {
    pub const fn new() -> Self {
        Self {
            server_fd: -1,
            server_watcher_id: 0,
            connections: [const { Connection::empty() }; MAX_CONNECTIONS],
            should_exit: false,
        }
    }

    /// Find a free connection slot.
    pub fn alloc_connection(&mut self) -> Option<usize> {
        for i in 0..MAX_CONNECTIONS {
            if self.connections[i].state == ConnState::Free {
                return Some(i);
            }
        }
        None
    }

    /// Get a mutable connection by index.
    pub fn connection_mut(&mut self, idx: usize) -> &mut Connection {
        &mut self.connections[idx]
    }

    /// Close and reset a connection.
    pub fn close_connection(&mut self, idx: usize, event_loop: &mut EventLoop) {
        let conn = &mut self.connections[idx];
        if conn.fd >= 0 {
            event_loop.io_stop_fd(conn.fd);
            tcp::close_fd(conn.fd);
        }
        conn.reset();
    }

    /// Try to parse the receive buffer of a connection.
    /// Returns true if the request (headers + body) is fully received.
    pub fn try_parse(&mut self, conn_idx: usize) -> bool {
        let conn = &mut self.connections[conn_idx];
        let complete = is_request_complete(&conn.recv_buf[..conn.recv_len], conn.recv_len);
        if complete {
            conn.parse_complete = true;
        }
        complete
    }

    /// Send an error response and close the connection.
    pub fn send_error(
        &mut self,
        conn_idx: usize,
        status: u16,
        event_loop: &mut EventLoop,
    ) {
        let fd = self.connections[conn_idx].fd;
        if fd >= 0 {
            let mut resp = HttpResponse::new();
            resp.status_code = status;
            resp.completed = true;
            write_response(fd, &resp);
        }
        self.close_connection(conn_idx, event_loop);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_get_with_query_and_headers() {
        let buf = b"GET /path?query=value HTTP/1.1\r\nHost: localhost\r\nAccept: */*\r\n\r\n";
        let (req, body_start) = parse_request(buf).unwrap();

        assert_eq!(req.method, Method::Get);
        assert_eq!(req.path, "/path");
        assert_eq!(req.query, "query=value");
        assert_eq!(req.headers().len(), 2);
        assert_eq!(req.headers()[0].name, "Host");
        assert_eq!(req.headers()[0].value, b"localhost");
        assert_eq!(req.headers()[1].name, "Accept");
        assert_eq!(req.body.len(), 0);
        assert_eq!(body_start, buf.len());
    }

    #[test]
    fn parse_post_with_body() {
        let buf = b"POST /submit HTTP/1.1\r\nContent-Length: 13\r\n\r\nHello, World!";
        let (req, body_start) = parse_request(buf).unwrap();

        assert_eq!(req.method, Method::Post);
        assert_eq!(req.path, "/submit");
        assert_eq!(req.query, "");
        assert_eq!(req.body, b"Hello, World!");
        assert_eq!(req.body_str(), "Hello, World!");
        assert_eq!(body_start, buf.len() - 13);
    }

    #[test]
    fn parse_path_without_query() {
        let buf = b"GET /about HTTP/1.1\r\nHost: localhost\r\n\r\n";
        let (req, _) = parse_request(buf).unwrap();

        assert_eq!(req.path, "/about");
        assert_eq!(req.query, "");
    }

    #[test]
    fn parse_all_methods() {
        for (method_str, expected) in [
            ("GET", Method::Get),
            ("POST", Method::Post),
            ("PUT", Method::Put),
            ("DELETE", Method::Delete),
            ("PATCH", Method::Patch),
            ("OPTIONS", Method::Options),
            ("HEAD", Method::Head),
        ] {
            let buf = format!("{} / HTTP/1.1\r\nHost: localhost\r\n\r\n", method_str);
            let (req, _) = parse_request(buf.as_bytes()).unwrap();
            assert_eq!(req.method, expected, "failed for {}", method_str);
        }
    }

    #[test]
    fn partial_request_returns_err() {
        // Incomplete headers (no \r\n\r\n terminator)
        let buf = b"GET /path HTTP/1.1\r\nHost: localhost";
        assert!(parse_request(buf).is_err());
    }

    #[test]
    fn empty_buffer_returns_err() {
        assert!(parse_request(b"").is_err());
    }

    #[test]
    fn malformed_request_returns_err() {
        let buf = b"\x00\x01\x02\r\n\r\n";
        assert!(parse_request(buf).is_err());
    }

    #[test]
    fn is_complete_no_body() {
        let buf = b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        assert!(is_request_complete(buf, buf.len()));
    }

    #[test]
    fn is_complete_with_full_body() {
        let buf = b"POST / HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello";
        assert!(is_request_complete(buf, buf.len()));
    }

    #[test]
    fn is_incomplete_partial_body() {
        let buf = b"POST / HTTP/1.1\r\nContent-Length: 10\r\n\r\nhello";
        assert!(!is_request_complete(buf, buf.len()));
    }

    #[test]
    fn is_incomplete_partial_headers() {
        let buf = b"GET / HTTP/1.1\r\nHost:";
        assert!(!is_request_complete(buf, buf.len()));
    }

    #[test]
    fn parse_multiple_headers() {
        let buf = b"GET / HTTP/1.1\r\n\
            Host: example.com\r\n\
            Content-Type: application/json\r\n\
            Authorization: Bearer token123\r\n\
            X-Custom: value\r\n\r\n";
        let (req, _) = parse_request(buf).unwrap();

        assert_eq!(req.headers().len(), 4);
        assert_eq!(req.headers()[0].name, "Host");
        assert_eq!(req.headers()[2].name, "Authorization");
        assert_eq!(req.headers()[2].value, b"Bearer token123");
    }
}
