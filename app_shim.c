/*
 * Shim for Zephyr kernel and socket functions that Rust can't link to directly.
 *
 * In Zephyr, __syscall functions expand to static inline wrappers when
 * CONFIG_USERSPACE is disabled (the common embedded case). These thin
 * non-inline wrappers give Rust FFI a real linkable symbol.
 *
 * Socket shims: On native_sim, Zephyr's socket API is accessed via macros
 * (e.g. socket() -> zsock_socket()) defined in <zephyr/net/socket.h>.
 * Rust FFI can't see C preprocessor macros, so we provide thin wrappers.
 * This also avoids glibc symbol conflicts on native_sim.
 */

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>

int64_t isere_k_uptime_get(void)
{
    return k_uptime_get();
}

int isere_socket(int domain, int type, int protocol)
{
    return zsock_socket(domain, type, protocol);
}

int isere_bind(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    return zsock_bind(fd, addr, addrlen);
}

int isere_listen(int fd, int backlog)
{
    return zsock_listen(fd, backlog);
}

int isere_accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
    return zsock_accept(fd, addr, addrlen);
}

ssize_t isere_recv(int fd, void *buf, size_t len, int flags)
{
    return zsock_recv(fd, buf, len, flags);
}

ssize_t isere_send(int fd, const void *buf, size_t len, int flags)
{
    return zsock_send(fd, buf, len, flags);
}

int isere_close(int fd)
{
    return zsock_close(fd);
}

int isere_setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    return zsock_setsockopt(fd, level, optname, optval, optlen);
}

int isere_fcntl(int fd, int cmd, int val)
{
    return zsock_fcntl(fd, cmd, val);
}

int isere_poll(struct zsock_pollfd *fds, int nfds, int timeout)
{
    return zsock_poll(fds, nfds, timeout);
}
