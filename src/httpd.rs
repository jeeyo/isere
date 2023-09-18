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

mod httpd {
}
