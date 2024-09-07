/* Copyright libuv project contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "libuv/uv.h"
#include "libuv/queue.h"

/* POSIX defines poll() as a portable way to wait on file descriptors.
 * Here we maintain a dynamically sized array of file descriptors and
 * events to pass as the first argument to poll().
 */

#include <stdint.h>
#include <errno.h>

#include "tcp.h"

#include "FreeRTOS.h"
#include "pvPortRealloc.h"

static void uv__pollfds_del(uv_loop_t* loop, int fd);

int uv__platform_loop_init(uv_loop_t* loop) {
  loop->poll_fds = NULL;
  loop->poll_fds_used = 0;
  loop->poll_fds_size = 0;
  loop->poll_fds_iterating = 0;
  return 0;
}

void uv__platform_loop_delete(uv_loop_t* loop) {
  vPortFree(loop->poll_fds);
  loop->poll_fds = NULL;
}

/* Remove the given fd from our poll fds array because no one
 * is interested in its events anymore.
 */
void uv__platform_invalidate_fd(uv_loop_t* loop, int fd) {
  size_t i;

  assert(fd >= 0);

  if (loop->poll_fds_iterating) {
    /* uv__io_poll is currently iterating.  Just invalidate fd.  */
    for (i = 0; i < loop->poll_fds_used; i++)
      if (loop->poll_fds[i].fd == fd) {
        loop->poll_fds[i].fd = -1;
        loop->poll_fds[i].events = 0;
        loop->poll_fds[i].revents = 0;
      }
  } else {
    /* uv__io_poll is not iterating.  Delete fd from the set.  */
    uv__pollfds_del(loop, fd);
  }
}

/* Allocate or dynamically resize our poll fds array.  */
static void uv__pollfds_maybe_resize(uv_loop_t* loop) {
  size_t i;
  size_t n;
  struct pollfd* p;

  if (loop->poll_fds_used < loop->poll_fds_size)
    return;

  n = loop->poll_fds_size ? loop->poll_fds_size * 2 : 64;
  p = pvPortRealloc(loop->poll_fds, n * sizeof(*loop->poll_fds));
  if (p == NULL)
    abort();

  loop->poll_fds = p;
  for (i = loop->poll_fds_size; i < n; i++) {
    loop->poll_fds[i].fd = -1;
    loop->poll_fds[i].events = 0;
    loop->poll_fds[i].revents = 0;
  }
  loop->poll_fds_size = n;
}

/* Primitive swap operation on poll fds array elements.  */
static void uv__pollfds_swap(uv_loop_t* loop, size_t l, size_t r) {
  struct pollfd pfd;
  pfd = loop->poll_fds[l];
  loop->poll_fds[l] = loop->poll_fds[r];
  loop->poll_fds[r] = pfd;
}

/* Add a watcher's fd to our poll fds array with its pending events.  */
static void uv__pollfds_add(uv_loop_t* loop, uv__io_t* w) {
  size_t i;
  struct pollfd* pe;

  if (loop->poll_fds_iterating) {
    return;
  }

  /* If the fd is already in the set just update its events.  */
  for (i = 0; i < loop->poll_fds_used; ++i) {
    if (loop->poll_fds[i].fd == w->fd) {
      loop->poll_fds[i].events = w->pevents;
      return;
    }
  }

  /* Otherwise, allocate a new slot in the set for the fd.  */
  uv__pollfds_maybe_resize(loop);
  pe = &loop->poll_fds[loop->poll_fds_used++];
  pe->fd = w->fd;
  pe->events = w->pevents;
}

/* Remove a watcher's fd from our poll fds array.  */
static void uv__pollfds_del(uv_loop_t* loop, int fd) {
  size_t i;

  if (loop->poll_fds_iterating) {
    return;
  }

  for (i = 0; i < loop->poll_fds_used;) {
    if (loop->poll_fds[i].fd == fd) {
      /* swap to last position and remove */
      --loop->poll_fds_used;
      uv__pollfds_swap(loop, i, loop->poll_fds_used);
      loop->poll_fds[loop->poll_fds_used].fd = -1;
      loop->poll_fds[loop->poll_fds_used].events = 0;
      loop->poll_fds[loop->poll_fds_used].revents = 0;
      /* This method is called with an fd of -1 to purge the invalidated fds,
       * so we may possibly have multiples to remove.
       */
      if (-1 != fd)
        return;
    } else {
      /* We must only increment the loop counter when the fds do not match.
       * Otherwise, when we are purging an invalidated fd, the value just
       * swapped here from the previous end of the array will be skipped.
       */
       ++i;
    }
  }
}

static unsigned int next_power_of_two(unsigned int val) {
  val -= 1;
  val |= val >> 1;
  val |= val >> 2;
  val |= val >> 4;
  val |= val >> 8;
  val |= val >> 16;
  val += 1;
  return val;
}

static void maybe_resize(uv_loop_t* loop, unsigned int len) {
  uv__io_t** watchers;
  void* fake_watcher_list;
  void* fake_watcher_count;
  unsigned int nwatchers;
  unsigned int i;

  if (len <= loop->nwatchers)
    return;

  /* Preserve fake watcher list and count at the end of the watchers */
  if (loop->watchers != NULL) {
    fake_watcher_list = loop->watchers[loop->nwatchers];
    fake_watcher_count = loop->watchers[loop->nwatchers + 1];
  } else {
    fake_watcher_list = NULL;
    fake_watcher_count = NULL;
  }

  nwatchers = next_power_of_two(len + 2) - 2;
  watchers = pvPortRealloc(loop->watchers,
                          (nwatchers + 2) * sizeof(loop->watchers[0]));

  if (watchers == NULL)
    abort();
  for (i = loop->nwatchers; i < nwatchers; i++)
    watchers[i] = NULL;
  watchers[nwatchers] = fake_watcher_list;
  watchers[nwatchers + 1] = fake_watcher_count;

  loop->watchers = watchers;
  loop->nwatchers = nwatchers;
}

void uv__io_start(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
  assert(0 == (events & ~(POLLIN | POLLOUT)));
  assert(0 != events);
  assert(w->fd >= 0);
  assert(w->fd < INT_MAX);

  w->pevents |= events;
  maybe_resize(loop, w->fd + 1);

  if (uv__queue_empty(&w->watcher_queue))
    uv__queue_insert_tail(&loop->watcher_queue, &w->watcher_queue);

  if (loop->watchers[w->fd] == NULL) {
    loop->watchers[w->fd] = w;
    loop->nfds++;
  }
}

void uv__io_stop(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
  assert(0 == (events & ~(POLLIN | POLLOUT)));
  assert(0 != events);

  if (w->fd == -1)
    return;

  assert(w->fd >= 0);

  /* Happens when uv__io_stop() is called on a handle that was never started. */
  if ((unsigned) w->fd >= loop->nwatchers)
    return;

  w->pevents &= ~events;

  if (w->pevents == 0) {
    uv__queue_remove(&w->watcher_queue);
    uv__queue_init(&w->watcher_queue);
    w->events = 0;

    if (w == loop->watchers[w->fd]) {
      assert(loop->nfds > 0);
      loop->watchers[w->fd] = NULL;
      loop->nfds--;
    }
  }
  else if (uv__queue_empty(&w->watcher_queue))
    uv__queue_insert_tail(&loop->watcher_queue, &w->watcher_queue);
}

void uv__io_poll(uv_loop_t* loop, int timeout)
{
  struct uv__queue* q;
  uv__io_t* w;
  struct pollfd* pe;
  int fd;

  if (loop->nfds == 0) {
    assert(uv__queue_empty(&loop->watcher_queue));
    return;
  }

  /* Take queued watchers and add their fds to our poll fds array.  */
  while (!uv__queue_empty(&loop->watcher_queue)) {
    q = uv__queue_head(&loop->watcher_queue);
    uv__queue_remove(q);
    uv__queue_init(q);

    w = uv__queue_data(q, uv__io_t, watcher_queue);
    assert(w->pevents != 0);
    assert(w->fd >= 0);
    assert(w->fd < (int) loop->nwatchers);

    uv__pollfds_add(loop, w);

    w->events = w->pevents;
  }

  int ready = isere_tcp_poll(loop->poll_fds, loop->poll_fds_used, timeout);
  if (ready < 0 && errno != EINTR) {
    return;
  }

  /* Tell uv__platform_invalidate_fd not to manipulate our array
   * while we are iterating over it.
   */
  loop->poll_fds_iterating = 1;

  /* Loop over the entire poll fds array looking for returned events.  */
  for (int i = 0; i < loop->poll_fds_used; i++)
  {
    pe = loop->poll_fds + i;
    int fd = pe->fd;

    /* Skip invalidated events, see uv__platform_invalidate_fd.  */
    if (fd == -1)
      continue;

    assert(fd >= 0);
    assert((unsigned) fd < loop->nwatchers);

    w = loop->watchers[fd];

    if (w == NULL) {
      /* File descriptor that we've stopped watching, ignore.  */
      uv__platform_invalidate_fd(loop, fd);
      continue;
    }

    /* Filter out events that user has not requested us to watch
      * (e.g. POLLNVAL).
      */
    // pe->revents &= w->pevents | POLLERR | POLLHUP;
    pe->revents &= w->pevents;

    if (pe->revents != 0) {
      w->cb(loop, w, pe->revents);
    }
  }

  loop->poll_fds_iterating = 0;
}
