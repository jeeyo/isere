use freertos_rust::{freertos_rs_delete_task, FreeRtosTaskHandle};
use log::{error, info};

extern crate alloc;
use alloc::{string::String, vec::Vec};

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

struct HttpdSocket {
  fd: cty::c_int,
  recvd: u32, // number of bytes received
  tsk: Option<FreeRtosTaskHandle>,
}

impl HttpdSocket {
  pub fn new() -> Self {
    Self {
      fd: -1,
      recvd: 0,
      tsk: None
    }
  }
}

pub struct Httpd {
  fd: cty::c_int,
  socks: Vec<HttpdSocket>,
}

impl Drop for Httpd {
  fn drop(&mut self) {
    for sock in &self.socks {
      if sock.tsk.is_some() {
        unsafe { freertos_rs_delete_task(sock.tsk.unwrap()); }
      }

      if sock.fd != -1 && unsafe { tcp_socket_close(sock.fd) } != 0 {
        error!("failed to close socket {}", sock.fd);
      }
    }
    // TODO: close all sockets & tasks
  }
}

impl Default for Httpd {
  fn default() -> Self {
    Self {
      fd: -1,
      socks: Vec::new(),
    }
  }
}

impl Httpd {
  const ISERE_HTTPD_PORT: u16 = 8080;

  pub fn poll(&mut self) {
    self.fd = unsafe { tcp_socket_new() };
    if self.fd < 0 {
      error!("unable to create new socket for server");
      unsafe { freertos_rs_delete_task(core::ptr::null()); }
      return;
    }

    if unsafe { tcp_listen(self.fd, Self::ISERE_HTTPD_PORT) } < 0 {
      error!("unable to listen to port {}", Self::ISERE_HTTPD_PORT);
      unsafe { freertos_rs_delete_task(core::ptr::null()); }
      return;
    }

    info!("listening on port {}", Self::ISERE_HTTPD_PORT);
  }
}
