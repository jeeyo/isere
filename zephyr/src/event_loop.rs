// Poll-based event loop for non-blocking I/O.
//
// Replaces the libuv subset (uv_poll.c) from the C implementation.
// Uses the same pattern: register watchers with fd + events + callback,
// then poll() in a loop to dispatch ready events.

use core::ffi::c_int;
use crate::platform::tcp::{self, PollFd, POLLIN, POLLOUT, POLLERR, POLLHUP};

/// Maximum number of file descriptors we can watch simultaneously.
const MAX_WATCHERS: usize = 16;

/// Callback type for I/O events.
pub type IoCallback = fn(watcher_id: usize, fd: c_int, events: i16, userdata: usize);

/// An I/O watcher entry.
struct Watcher {
    fd: c_int,
    events: i16,        // events we're interested in (POLLIN, POLLOUT)
    cb: IoCallback,
    userdata: usize,    // opaque pointer-sized data (e.g., connection index)
    active: bool,
}

impl Watcher {
    const fn empty() -> Self {
        Self {
            fd: -1,
            events: 0,
            cb: null_callback,
            userdata: 0,
            active: false,
        }
    }
}

fn null_callback(_watcher_id: usize, _fd: c_int, _events: i16, _userdata: usize) {}

/// The event loop holding all watchers and poll state.
pub struct EventLoop {
    watchers: [Watcher; MAX_WATCHERS],
    poll_fds: [PollFd; MAX_WATCHERS],
    /// Mapping from poll_fds index to watchers index.
    poll_map: [usize; MAX_WATCHERS],
    num_active: usize,
}

impl EventLoop {
    pub const fn new() -> Self {
        Self {
            watchers: [const { Watcher::empty() }; MAX_WATCHERS],
            poll_fds: [const {
                PollFd {
                    fd: -1,
                    events: 0,
                    revents: 0,
                }
            }; MAX_WATCHERS],
            poll_map: [0; MAX_WATCHERS],
            num_active: 0,
        }
    }

    /// Register a new I/O watcher. Returns watcher ID.
    pub fn io_start(
        &mut self,
        fd: c_int,
        events: i16,
        cb: IoCallback,
        userdata: usize,
    ) -> Option<usize> {
        // Check if this fd is already watched
        for i in 0..MAX_WATCHERS {
            if self.watchers[i].active && self.watchers[i].fd == fd {
                // Update existing watcher
                self.watchers[i].events = events;
                self.watchers[i].cb = cb;
                self.watchers[i].userdata = userdata;
                self.rebuild_poll_fds();
                return Some(i);
            }
        }

        // Find a free slot
        for i in 0..MAX_WATCHERS {
            if !self.watchers[i].active {
                self.watchers[i] = Watcher {
                    fd,
                    events,
                    cb,
                    userdata,
                    active: true,
                };
                self.rebuild_poll_fds();
                return Some(i);
            }
        }

        None // No free slots
    }

    /// Remove a watcher by ID.
    pub fn io_stop(&mut self, watcher_id: usize) {
        if watcher_id < MAX_WATCHERS && self.watchers[watcher_id].active {
            self.watchers[watcher_id].active = false;
            self.watchers[watcher_id].fd = -1;
            self.rebuild_poll_fds();
        }
    }

    /// Remove a watcher by fd.
    pub fn io_stop_fd(&mut self, fd: c_int) {
        for i in 0..MAX_WATCHERS {
            if self.watchers[i].active && self.watchers[i].fd == fd {
                self.watchers[i].active = false;
                self.watchers[i].fd = -1;
            }
        }
        self.rebuild_poll_fds();
    }

    /// Rebuild the poll_fds array from active watchers.
    fn rebuild_poll_fds(&mut self) {
        self.num_active = 0;
        for i in 0..MAX_WATCHERS {
            if self.watchers[i].active {
                let idx = self.num_active;
                self.poll_fds[idx] = PollFd {
                    fd: self.watchers[i].fd,
                    events: self.watchers[i].events,
                    revents: 0,
                };
                self.poll_map[idx] = i;
                self.num_active += 1;
            }
        }
    }

    /// Poll for events and dispatch callbacks. Returns number of events dispatched.
    pub fn poll(&mut self, timeout_ms: c_int) -> i32 {
        if self.num_active == 0 {
            return 0;
        }

        // Clear revents
        for i in 0..self.num_active {
            self.poll_fds[i].revents = 0;
        }

        let ready = match tcp::poll_fds(&mut self.poll_fds[..self.num_active], timeout_ms) {
            Ok(n) => n,
            Err(_) => return -1,
        };

        if ready <= 0 {
            return 0;
        }

        // Collect events before dispatching to avoid borrow issues.
        // We store (watcher_index, fd, revents) tuples.
        let mut events_buf: [(usize, c_int, i16); MAX_WATCHERS] = [(0, -1, 0); MAX_WATCHERS];
        let mut event_count = 0;

        for i in 0..self.num_active {
            let revents = self.poll_fds[i].revents;
            if revents != 0 {
                let watcher_idx = self.poll_map[i];
                let w = &self.watchers[watcher_idx];
                // Filter to events the watcher requested, plus error conditions
                let filtered = revents & (w.events | POLLERR | POLLHUP);
                if filtered != 0 {
                    events_buf[event_count] = (watcher_idx, w.fd, filtered);
                    event_count += 1;
                }
            }
        }

        // Dispatch callbacks
        let mut dispatched = 0;
        for i in 0..event_count {
            let (watcher_idx, fd, filtered) = events_buf[i];
            if self.watchers[watcher_idx].active {
                let cb = self.watchers[watcher_idx].cb;
                let userdata = self.watchers[watcher_idx].userdata;
                cb(watcher_idx, fd, filtered, userdata);
                dispatched += 1;
            }
        }

        dispatched
    }
}
