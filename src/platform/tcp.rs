// TCP socket abstraction over Zephyr's BSD socket API.
//
// Zephyr provides POSIX-compatible sockets when CONFIG_NET_SOCKETS_POSIX_NAMES=y.
// This module wraps those raw syscalls into safe Rust types.

use core::ffi::c_int;

pub const MAX_CONNECTIONS: usize = 12;

// Zephyr socket FFI (POSIX-compatible when CONFIG_NET_SOCKETS_POSIX_NAMES=y)
mod ffi {
    use core::ffi::{c_int, c_void};

    pub const AF_INET: c_int = 2;
    pub const SOCK_STREAM: c_int = 1;
    pub const IPPROTO_TCP: c_int = 6;
    pub const SOL_SOCKET: c_int = 1;
    pub const SO_REUSEADDR: c_int = 2;
    pub const INADDR_ANY: u32 = 0;

    pub const O_NONBLOCK: c_int = 0x800; // may vary by arch; Zephyr ARM typically uses this
    pub const F_GETFL: c_int = 3;
    pub const F_SETFL: c_int = 4;

    pub const POLLIN: i16 = 0x001;
    pub const POLLOUT: i16 = 0x004;
    pub const POLLERR: i16 = 0x008;
    pub const POLLHUP: i16 = 0x010;

    #[repr(C)]
    pub struct SockAddrIn {
        pub sin_family: u16,
        pub sin_port: u16,
        pub sin_addr: InAddr,
        pub sin_zero: [u8; 8],
    }

    #[repr(C)]
    pub struct InAddr {
        pub s_addr: u32,
    }

    #[repr(C)]
    pub struct PollFd {
        pub fd: c_int,
        pub events: i16,
        pub revents: i16,
    }

    // Use Zephyr's zsock_* functions to avoid linking to glibc on native_sim.
    // On native_sim (host executable), `extern "C" fn socket()` would resolve
    // to glibc's socket(), bypassing Zephyr's network stack entirely.
    // The zsock_* symbols are always Zephyr's implementation on all targets.
    extern "C" {
        #[link_name = "zsock_socket"]
        pub fn socket(domain: c_int, sock_type: c_int, protocol: c_int) -> c_int;
        #[link_name = "zsock_bind"]
        pub fn bind(fd: c_int, addr: *const SockAddrIn, addrlen: u32) -> c_int;
        #[link_name = "zsock_listen"]
        pub fn listen(fd: c_int, backlog: c_int) -> c_int;
        #[link_name = "zsock_accept"]
        pub fn accept(fd: c_int, addr: *mut SockAddrIn, addrlen: *mut u32) -> c_int;
        #[link_name = "zsock_recv"]
        pub fn recv(fd: c_int, buf: *mut c_void, len: usize, flags: c_int) -> isize;
        #[link_name = "zsock_send"]
        pub fn send(fd: c_int, buf: *const c_void, len: usize, flags: c_int) -> isize;
        #[link_name = "zsock_close"]
        pub fn close(fd: c_int) -> c_int;
        #[link_name = "zsock_setsockopt"]
        pub fn setsockopt(
            fd: c_int,
            level: c_int,
            optname: c_int,
            optval: *const c_void,
            optlen: u32,
        ) -> c_int;
        #[link_name = "zsock_fcntl"]
        pub fn fcntl(fd: c_int, cmd: c_int, ...) -> c_int;
        #[link_name = "zsock_poll"]
        pub fn poll(fds: *mut PollFd, nfds: u32, timeout: c_int) -> c_int;
    }
}

pub use ffi::{PollFd, POLLERR, POLLHUP, POLLIN, POLLOUT};

/// Convert port to network byte order (big-endian).
fn htons(val: u16) -> u16 {
    val.to_be()
}

#[derive(Debug)]
pub enum TcpError {
    SocketCreate,
    SetReuse,
    SetNonblock,
    Bind,
    Listen,
    Accept,
    WouldBlock,
    Closed,
    RecvError,
    SendError,
    PollError,
}

/// Create a new TCP socket. Returns the file descriptor.
pub fn socket_new() -> Result<c_int, TcpError> {
    let fd = unsafe { ffi::socket(ffi::AF_INET, ffi::SOCK_STREAM, ffi::IPPROTO_TCP) };
    if fd < 0 {
        Err(TcpError::SocketCreate)
    } else {
        Ok(fd)
    }
}

/// Set SO_REUSEADDR on a socket.
pub fn socket_set_reuse(fd: c_int) -> Result<(), TcpError> {
    let val: c_int = 1;
    let ret = unsafe {
        ffi::setsockopt(
            fd,
            ffi::SOL_SOCKET,
            ffi::SO_REUSEADDR,
            &val as *const c_int as *const core::ffi::c_void,
            core::mem::size_of::<c_int>() as u32,
        )
    };
    if ret < 0 {
        Err(TcpError::SetReuse)
    } else {
        Ok(())
    }
}

/// Set a socket to non-blocking mode.
pub fn socket_set_nonblock(fd: c_int) -> Result<(), TcpError> {
    let flags = unsafe { ffi::fcntl(fd, ffi::F_GETFL, 0) };
    if flags < 0 {
        return Err(TcpError::SetNonblock);
    }
    let ret = unsafe { ffi::fcntl(fd, ffi::F_SETFL, flags | ffi::O_NONBLOCK) };
    if ret < 0 {
        Err(TcpError::SetNonblock)
    } else {
        Ok(())
    }
}

/// Bind and listen on a port.
pub fn listen_on(fd: c_int, port: u16) -> Result<(), TcpError> {
    let addr = ffi::SockAddrIn {
        sin_family: ffi::AF_INET as u16,
        sin_port: htons(port),
        sin_addr: ffi::InAddr {
            s_addr: ffi::INADDR_ANY,
        },
        sin_zero: [0u8; 8],
    };

    let ret = unsafe {
        ffi::bind(
            fd,
            &addr as *const ffi::SockAddrIn,
            core::mem::size_of::<ffi::SockAddrIn>() as u32,
        )
    };
    if ret < 0 {
        return Err(TcpError::Bind);
    }

    let ret = unsafe { ffi::listen(fd, MAX_CONNECTIONS as c_int) };
    if ret < 0 {
        return Err(TcpError::Listen);
    }

    Ok(())
}

/// Accept a new connection. Returns the new fd.
pub fn accept_conn(fd: c_int) -> Result<c_int, TcpError> {
    let mut addr = ffi::SockAddrIn {
        sin_family: 0,
        sin_port: 0,
        sin_addr: ffi::InAddr { s_addr: 0 },
        sin_zero: [0u8; 8],
    };
    let mut addr_len = core::mem::size_of::<ffi::SockAddrIn>() as u32;

    let newfd = unsafe { ffi::accept(fd, &mut addr, &mut addr_len) };
    if newfd < 0 {
        return Err(TcpError::Accept);
    }

    Ok(newfd)
}

/// Receive data from a socket. Returns bytes read, or error.
pub fn recv_data(fd: c_int, buf: &mut [u8]) -> Result<usize, TcpError> {
    let n = unsafe {
        ffi::recv(
            fd,
            buf.as_mut_ptr() as *mut core::ffi::c_void,
            buf.len(),
            0,
        )
    };
    if n < 0 {
        // TODO: check errno for EAGAIN/EWOULDBLOCK
        Err(TcpError::WouldBlock)
    } else if n == 0 {
        Err(TcpError::Closed)
    } else {
        Ok(n as usize)
    }
}

/// Send data to a socket. Returns bytes written.
pub fn send_data(fd: c_int, buf: &[u8]) -> Result<usize, TcpError> {
    let n = unsafe {
        ffi::send(
            fd,
            buf.as_ptr() as *const core::ffi::c_void,
            buf.len(),
            0,
        )
    };
    if n < 0 {
        Err(TcpError::SendError)
    } else {
        Ok(n as usize)
    }
}

/// Send all data, retrying partial writes.
pub fn send_all(fd: c_int, buf: &[u8]) -> Result<(), TcpError> {
    let mut offset = 0;
    while offset < buf.len() {
        match send_data(fd, &buf[offset..]) {
            Ok(n) => offset += n,
            Err(e) => return Err(e),
        }
    }
    Ok(())
}

/// Close a socket.
pub fn close_fd(fd: c_int) {
    unsafe {
        ffi::close(fd);
    }
}

/// Poll file descriptors for events.
pub fn poll_fds(fds: &mut [PollFd], timeout_ms: c_int) -> Result<c_int, TcpError> {
    let n = unsafe { ffi::poll(fds.as_mut_ptr(), fds.len() as u32, timeout_ms) };
    if n < 0 {
        Err(TcpError::PollError)
    } else {
        Ok(n)
    }
}
