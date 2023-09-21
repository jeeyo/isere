use freertos_rust::{freertos_rs_delete_task, FreeRtosTaskHandle};
use log::error;

extern crate alloc;
use alloc::{string::String, vec::Vec, format};

extern "C" {
  pub fn tcp_socket_new() -> cty::c_int;
  pub fn tcp_socket_close(sock: cty::c_int) -> cty::c_int;
  pub fn tcp_listen(sock: cty::c_int, port: cty::uint16_t) -> cty::c_int;
  pub fn tcp_accept(sock: cty::c_int, ip_addr: *mut cty::c_char) -> cty::c_int;
  pub fn tcp_recv(sock: cty::c_int, buf: *mut cty::c_char, len: cty::size_t) -> cty::c_int;
  pub fn tcp_write(sock: cty::c_int, buf: *const cty::c_char, len: cty::size_t) -> cty::c_int;
}

/*
  Response object:
  ```
  {
    "isBase64Encoded": false, // Set to `true` for binary support.
    "statusCode": 200,
    "headers": {
        "header1Name": "header1Value",
        "header2Name": "header2Value",
    },
    "body": "...",
  }
  ```
*/
#[derive(Debug)]
pub struct Response {
  pub is_base64_encoded: bool,
  pub status_code: u32,
  pub headers: Vec<(String, String)>,
  pub body: String,
}

pub struct Httpd {
  socks: Vec<HttpdSocket>,
}

struct HttpdSocket {
  sock: cty::c_int,
  recvd: u32, // number of bytes received
  tsk: Option<FreeRtosTaskHandle>,
}

impl Drop for Httpd {
  fn drop(&mut self) {
    for sock in &self.socks {
      if sock.tsk.is_some() {
        unsafe { freertos_rs_delete_task(sock.tsk.unwrap()); }
      }

      if sock.sock != -1 && unsafe { tcp_socket_close(sock.sock) } != 0 {
        error!("failed to close socket {}", sock.sock);
      }
    }
    // TODO: close all sockets & tasks
  }
}

impl Default for Httpd {
  fn default() -> Self {
    Self { socks: Vec::new() }
  }
}

impl Httpd {
  
}
