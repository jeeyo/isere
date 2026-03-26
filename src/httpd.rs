// HTTP/1.1 server for isere.
//
// Replaces httpd.c + llhttp with a Rust-native implementation.
// Uses httparse for zero-copy HTTP parsing (no_std compatible).
// Non-blocking sockets with poll()-based event loop.

use core::ffi::c_int;
use core::fmt::Write as FmtWrite;

use crate::event_loop::{EventLoop, IoCallback};
use crate::platform::tcp::{self, POLLIN};

pub const HTTPD_PORT: u16 = 8080;
pub const MAX_CONNECTIONS: usize = 12;
pub const MAX_HEADERS: usize = 16;
pub const MAX_REQUEST_SIZE: usize = 2048;
pub const MAX_BODY_SIZE: usize = 512;
pub const MAX_METHOD_LEN: usize = 16;
pub const MAX_PATH_LEN: usize = 256;
pub const MAX_HEADER_NAME_LEN: usize = 64;
pub const MAX_HEADER_VALUE_LEN: usize = 512;
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

/// A parsed HTTP request header.
#[derive(Clone)]
pub struct Header {
    pub name: [u8; MAX_HEADER_NAME_LEN],
    pub name_len: usize,
    pub value: [u8; MAX_HEADER_VALUE_LEN],
    pub value_len: usize,
}

impl Header {
    pub const fn empty() -> Self {
        Self {
            name: [0u8; MAX_HEADER_NAME_LEN],
            name_len: 0,
            value: [0u8; MAX_HEADER_VALUE_LEN],
            value_len: 0,
        }
    }

    pub fn name_str(&self) -> &str {
        core::str::from_utf8(&self.name[..self.name_len]).unwrap_or("")
    }

    pub fn value_str(&self) -> &str {
        core::str::from_utf8(&self.value[..self.value_len]).unwrap_or("")
    }
}

/// A parsed HTTP request.
pub struct HttpRequest {
    pub method: Method,
    pub path: [u8; MAX_PATH_LEN],
    pub path_len: usize,
    pub query: [u8; MAX_PATH_LEN],
    pub query_len: usize,
    pub headers: [Header; MAX_HEADERS],
    pub num_headers: usize,
    pub body: [u8; MAX_BODY_SIZE],
    pub body_len: usize,
}

impl HttpRequest {
    pub const fn new() -> Self {
        Self {
            method: Method::Unknown,
            path: [0u8; MAX_PATH_LEN],
            path_len: 0,
            query: [0u8; MAX_PATH_LEN],
            query_len: 0,
            headers: [const { Header::empty() }; MAX_HEADERS],
            num_headers: 0,
            body: [0u8; MAX_BODY_SIZE],
            body_len: 0,
        }
    }

    pub fn path_str(&self) -> &str {
        core::str::from_utf8(&self.path[..self.path_len]).unwrap_or("/")
    }

    pub fn query_str(&self) -> &str {
        core::str::from_utf8(&self.query[..self.query_len]).unwrap_or("")
    }

    pub fn method_str(&self) -> &str {
        self.method.as_str()
    }

    pub fn body_str(&self) -> &str {
        core::str::from_utf8(&self.body[..self.body_len]).unwrap_or("")
    }
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

/// State of an HTTP connection.
#[derive(Clone, Copy, PartialEq)]
pub enum ConnState {
    Free,
    Reading,
    Processing,
    Writing,
}

/// A connection slot in the server.
pub struct Connection {
    pub fd: c_int,
    pub state: ConnState,
    pub watcher_id: usize,

    // receive buffer for incremental parsing
    pub recv_buf: [u8; MAX_REQUEST_SIZE],
    pub recv_len: usize,

    // parsed request
    pub request: HttpRequest,
    pub parse_complete: bool,

    // response
    pub response: HttpResponse,

    // JS context index (Phase 5: used to track JS execution)
    pub js_context_id: i32,
}

impl Connection {
    pub const fn empty() -> Self {
        Self {
            fd: -1,
            state: ConnState::Free,
            watcher_id: 0,
            recv_buf: [0u8; MAX_REQUEST_SIZE],
            recv_len: 0,
            request: HttpRequest::new(),
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
        self.request = HttpRequest::new();
        self.parse_complete = false;
        self.response = HttpResponse::new();
        self.js_context_id = -1;
    }
}

/// Parse an HTTP request from a buffer using httparse.
///
/// Returns Ok(body_offset) if headers are complete, Err(()) if incomplete or malformed.
fn parse_request(buf: &[u8], req: &mut HttpRequest) -> Result<usize, ()> {
    let mut headers = [httparse::EMPTY_HEADER; MAX_HEADERS];
    let mut parsed = httparse::Request::new(&mut headers);

    match parsed.parse(buf) {
        Ok(httparse::Status::Complete(body_offset)) => {
            // Method
            if let Some(method) = parsed.method {
                req.method = Method::from_bytes(method.as_bytes());
            }

            // Path and query string
            if let Some(path) = parsed.path {
                let path_bytes = path.as_bytes();
                if let Some(q_pos) = path_bytes.iter().position(|&b| b == b'?') {
                    let plen = q_pos.min(MAX_PATH_LEN);
                    req.path[..plen].copy_from_slice(&path_bytes[..plen]);
                    req.path_len = plen;

                    let query = &path_bytes[q_pos + 1..];
                    let qlen = query.len().min(MAX_PATH_LEN);
                    req.query[..qlen].copy_from_slice(&query[..qlen]);
                    req.query_len = qlen;
                } else {
                    let plen = path_bytes.len().min(MAX_PATH_LEN);
                    req.path[..plen].copy_from_slice(&path_bytes[..plen]);
                    req.path_len = plen;
                }
            }

            // Headers
            req.num_headers = 0;
            for h in parsed.headers.iter() {
                if req.num_headers >= MAX_HEADERS {
                    break;
                }
                let name_len = h.name.len().min(MAX_HEADER_NAME_LEN);
                req.headers[req.num_headers].name[..name_len]
                    .copy_from_slice(&h.name.as_bytes()[..name_len]);
                req.headers[req.num_headers].name_len = name_len;

                let value_len = h.value.len().min(MAX_HEADER_VALUE_LEN);
                req.headers[req.num_headers].value[..value_len]
                    .copy_from_slice(&h.value[..value_len]);
                req.headers[req.num_headers].value_len = value_len;

                req.num_headers += 1;
            }

            Ok(body_offset)
        }
        Ok(httparse::Status::Partial) => Err(()), // Need more data
        Err(_) => Err(()),                         // Malformed request
    }
}

/// Get Content-Length from parsed request headers.
fn get_content_length(req: &HttpRequest) -> usize {
    for i in 0..req.num_headers {
        let name = &req.headers[i].name[..req.headers[i].name_len];
        // Case-insensitive compare for "Content-Length" / "content-length"
        if name.len() == 14 && name.eq_ignore_ascii_case(b"content-length") {
            let val_str = core::str::from_utf8(&req.headers[i].value[..req.headers[i].value_len]);
            if let Ok(s) = val_str {
                if let Ok(n) = s.trim().parse::<usize>() {
                    return n;
                }
            }
        }
    }
    0
}

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

/// Write a u16 to a buffer as ASCII decimal. Returns number of bytes written.
fn write_u16(val: u16, buf: &mut [u8]) -> usize {
    write_usize(val as usize, buf)
}

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

/// The HTTP server.
pub struct HttpServer {
    pub server_fd: c_int,
    pub server_watcher_id: usize,
    pub connections: [Connection; MAX_CONNECTIONS],
    pub should_exit: bool,
}

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

    /// Get a connection by index.
    pub fn connection(&self, idx: usize) -> &Connection {
        &self.connections[idx]
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
    /// Returns true if parsing is complete.
    pub fn try_parse(&mut self, conn_idx: usize) -> bool {
        let conn = &mut self.connections[conn_idx];
        let buf = &conn.recv_buf[..conn.recv_len];

        match parse_request(buf, &mut conn.request) {
            Ok(body_start) => {
                // Copy body if present
                let content_length = get_content_length(&conn.request);
                if content_length > 0 && body_start < conn.recv_len {
                    let available = conn.recv_len - body_start;
                    let body_len = available.min(content_length).min(MAX_BODY_SIZE);
                    conn.request.body[..body_len]
                        .copy_from_slice(&conn.recv_buf[body_start..body_start + body_len]);
                    conn.request.body_len = body_len;

                    // Check if we have the full body
                    if available >= content_length {
                        conn.parse_complete = true;
                        return true;
                    }
                    // Need more data for body
                    return false;
                }
                // No body expected
                conn.parse_complete = true;
                true
            }
            Err(()) => false, // Need more data or parse error
        }
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
