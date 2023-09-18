extern "C" {
  pub fn tcp_init();
  pub fn tcp_deinit();
  pub fn tcp_socket_new();
  pub fn tcp_socket_close(sock: cty::c_int);
  pub fn tcp_listen(sock: cty::c_int, port: cty::uint16_t);
  pub fn tcp_accept(sock: cty::c_int, ip_addr: *mut cty::c_char);
  pub fn tcp_recv(sock: cty::c_int, buf: *mut cty::c_char, len: cty::size_t);
  pub fn tcp_write(sock: cty::c_int, buf: *const cty::c_char, len: cty::size_t);
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
struct Response {
  is_base64_encoded: bool,
  status_code: u32,
  headers: Vec<(String<64>, String<1024>), 10>,
  body: String<1024>,
}

mod httpd {
}
