#![no_std]

extern crate zephyr;

use zephyr::printk;

mod platform;
mod event_loop;
mod httpd;
mod http_handler;
mod js;

use event_loop::EventLoop;
use httpd::{HttpServer, ConnState, HTTPD_PORT, MAX_REQUEST_SIZE, write_response};
use platform::tcp::{self, POLLIN};

const APP_NAME: &str = "isere";
const APP_VERSION: &str = "0.1.0";

/// Global mutable state — in a real Zephyr app these would be in the main thread.
/// Zephyr's single-threaded Rust model means this is safe for our use case.
static mut EVENT_LOOP: EventLoop = EventLoop::new();
static mut SERVER: HttpServer = HttpServer::new();

#[no_mangle]
extern "C" fn rust_main() {
    printk!("{} v{} starting on Zephyr OS (Rust)\n", APP_NAME, APP_VERSION);

    if let Err(()) = run_server() {
        printk!("{}: server exited with error\n", APP_NAME);
    }
}

fn run_server() -> Result<(), ()> {
    // Create server socket
    let server_fd = tcp::socket_new().map_err(|_| {
        printk!("Failed to create server socket\n");
    })?;

    tcp::socket_set_reuse(server_fd).map_err(|_| {
        printk!("Failed to set socket reuse\n");
    })?;

    tcp::socket_set_nonblock(server_fd).map_err(|_| {
        printk!("Failed to set non-blocking\n");
    })?;

    tcp::listen_on(server_fd, HTTPD_PORT).map_err(|_| {
        printk!("Failed to listen on port {}\n", HTTPD_PORT);
    })?;

    printk!("Listening on port {}\n", HTTPD_PORT);

    unsafe {
        SERVER.server_fd = server_fd;

        // Register server socket for incoming connections
        SERVER.server_watcher_id = EVENT_LOOP
            .io_start(server_fd, POLLIN, on_server_readable, 0)
            .ok_or_else(|| {
                printk!("Failed to register server watcher\n");
            })?;
    }

    // Main event loop
    loop {
        unsafe {
            if SERVER.should_exit {
                break;
            }

            // Poll for I/O events (5ms timeout, same as C version)
            EVENT_LOOP.poll(5);

            // Process any connections that have completed HTTP parsing
            process_pending_requests();
        }
    }

    // Cleanup
    tcp::close_fd(server_fd);
    Ok(())
}

/// Callback when the server socket is readable (new connection incoming).
fn on_server_readable(_watcher_id: usize, fd: core::ffi::c_int, _events: i16, _userdata: usize) {
    let newfd = match tcp::accept_conn(fd) {
        Ok(fd) => fd,
        Err(_) => return,
    };

    // Set non-blocking
    if tcp::socket_set_nonblock(newfd).is_err() {
        tcp::close_fd(newfd);
        return;
    }

    unsafe {
        // Allocate a connection slot
        let conn_idx = match SERVER.alloc_connection() {
            Some(idx) => idx,
            None => {
                // No free slots — reject
                let _ = tcp::send_all(newfd, b"HTTP/1.1 503 Service Unavailable\r\n\r\n");
                tcp::close_fd(newfd);
                return;
            }
        };

        let conn = SERVER.connection_mut(conn_idx);
        conn.fd = newfd;
        conn.state = ConnState::Reading;

        // Register for read events
        if let Some(wid) = EVENT_LOOP.io_start(newfd, POLLIN, on_client_readable, conn_idx) {
            conn.watcher_id = wid;
        } else {
            conn.reset();
            tcp::close_fd(newfd);
        }
    }
}

/// Callback when a client socket is readable (data available).
fn on_client_readable(_watcher_id: usize, fd: core::ffi::c_int, events: i16, userdata: usize) {
    let conn_idx = userdata;

    unsafe {
        let conn = SERVER.connection_mut(conn_idx);

        if conn.state != ConnState::Reading {
            return;
        }

        // Read data into connection buffer
        let space = MAX_REQUEST_SIZE - conn.recv_len;
        if space == 0 {
            SERVER.send_error(conn_idx, 413, &mut EVENT_LOOP);
            return;
        }

        let buf_start = conn.recv_len;
        match tcp::recv_data(fd, &mut conn.recv_buf[buf_start..buf_start + space]) {
            Ok(n) => {
                conn.recv_len += n;
            }
            Err(tcp::TcpError::WouldBlock) => return,
            Err(tcp::TcpError::Closed) | Err(_) => {
                SERVER.close_connection(conn_idx, &mut EVENT_LOOP);
                return;
            }
        }

        // Try to parse the request
        if SERVER.try_parse(conn_idx) {
            // Stop watching for reads — we'll process this request
            let conn = SERVER.connection_mut(conn_idx);
            EVENT_LOOP.io_stop_fd(conn.fd);
            conn.state = ConnState::Processing;
        }
    }
}

/// Process connections that have finished parsing their HTTP request.
unsafe fn process_pending_requests() {
    for i in 0..httpd::MAX_CONNECTIONS {
        let state = SERVER.connections[i].state;
        if state != ConnState::Processing {
            continue;
        }

        // Execute JS handler
        let conn = SERVER.connection_mut(i);
        match http_handler::handle_request(conn) {
            Ok(()) => {
                // Write the response
                let fd = conn.fd;
                write_response(fd, &conn.response);
            }
            Err(()) => {
                // JS error — send 500
                let fd = conn.fd;
                let mut resp = httpd::HttpResponse::new();
                resp.status_code = 500;
                resp.set_body(b"Internal Server Error");
                resp.completed = true;
                write_response(fd, &resp);
            }
        }

        // Close connection (Connection: close)
        SERVER.close_connection(i, &mut EVENT_LOOP);
    }
}
