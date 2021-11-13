// traditional read then write + kevent for Darwin platforms
#ifdef __APPLE__

#include "utils.h"
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#define BUFFSZ (1024 * 1024)

struct buff_info {
  void *data;
  size_t filled;
  size_t written;
  bool srcclose;
  bool dstclose;
};

struct fwd_inst {
  size_t buffsz;
  struct buff_info b12; // fd1 -> fd2
  struct buff_info b21; // fd2 -> fd1
  int fd1;
  int fd2;
  int kq;
};

// automatically infer READ if bi->filled == 0, else WRITE.
// reset `filled` upon complete WRITE.
// will call err on error/EOF.
static void do_forward(size_t buffsz, struct buff_info *bi, int srcfd, int dstfd);

static inline void swap(int *a, int *b) {
  int c = *a;
  *a = *b;
  *b = c;
}

void *init_forwarder(int fd1, int fd2) {
  struct fwd_inst *inst = calloc(sizeof(struct fwd_inst), 1);
  if (!inst)
    err(1, "Error allocating forwarder instance");

  size_t ps = getpagesize();
  inst->buffsz = (BUFFSZ / ps) * ps;
  inst->b12.data = mmap(NULL, inst->buffsz, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
  if (inst->b12.data == MAP_FAILED)
    err(1, "Error allocating buffer for socket 1");
  inst->b21.data = mmap(NULL, inst->buffsz, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
  if (inst->b21.data == MAP_FAILED)
    err(1, "Error allocating buffer for socket 2");

  inst->fd1 = fd1;
  inst->fd2 = fd2;

  set_fd_flags(fd1, true, O_NONBLOCK);
  set_fd_flags(fd2, true, O_NONBLOCK);

  inst->kq = kqueue();

  return inst;
}

void run_forwarder(void *inst) {
  struct fwd_inst *self = inst;
  for (;;) {
    // if the destination FD has pending writes, then we won't be reading from the src FD
    struct kevent kevch[2] = {0};
    struct kevent kevres[4];
    int kevchlen = 0;

    // do read if and only if buffer == empty
    // conversely, do write if and only if there is unfinished writes
    if (!self->b12.filled && !self->b12.srcclose)
      EV_SET(&kevch[kevchlen++], self->fd1, EVFILT_READ, EV_ADD | EV_ENABLE | EV_DISPATCH, 0, 0, 0);
    else if (self->b12.filled && !self->b12.dstclose)
      EV_SET(&kevch[kevchlen++], self->fd2, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_DISPATCH, 0, 0, 0);
    if (!self->b21.filled && !self->b21.srcclose)
      EV_SET(&kevch[kevchlen++], self->fd2, EVFILT_READ, EV_ADD | EV_ENABLE | EV_DISPATCH, 0, 0, 0);
    else if (self->b21.filled && !self->b21.dstclose)
      EV_SET(&kevch[kevchlen++], self->fd1, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_DISPATCH, 0, 0, 0);

    if (kevchlen == 0)
      exit(0);

    int kevreslen = kevent(self->kq, kevch, kevchlen, kevres, 4, NULL);
    if (kevreslen < 0) {
      if (errno == EINTR)
        continue;
      err(1, "kevent error");
    }

    for (int i = 0; i < kevreslen; ++i) {
      int srcfd = kevres[i].ident;
      int dstfd = srcfd == self->fd1 ? self->fd2 : self->fd1;
      if (kevres[i].filter == EVFILT_WRITE)
        swap(&srcfd, &dstfd);
      struct buff_info *bi = srcfd == self->fd1 ? &self->b12 : &self->b21;
      do_forward(self->buffsz, bi, srcfd, dstfd);
    }
  }
}

static void do_forward(size_t buffsz, struct buff_info *bi, int srcfd, int dstfd) {
  if (!bi->filled) {
    ssize_t rd = read(srcfd, bi->data, buffsz);
    if (rd < 0) {
      switch (errno) {
      case EAGAIN:
      case EINTR:
        return;
      default:
        err(1, "Read error");
      }
    } else if (rd == 0) {
      shutdown(dstfd, SHUT_RD);
      bi->srcclose = true;
    }
    bi->written = 0;
    bi->filled = rd;
  }

  if (bi->filled) {
    ssize_t wr = write(dstfd, (void *)((uintptr_t)bi->data + bi->written), bi->filled - bi->written);
    if (wr < 0) {
      switch (errno) {
      case EAGAIN:
      case EINTR:
        return;
      default:
        err(1, "Write error");
      }
    } else if (wr == 0) {
      shutdown(srcfd, SHUT_RDWR);
      bi->srcclose = bi->dstclose = true;
    }
    bi->written += wr;
    if (bi->written == bi->filled) {
      bi->filled = 0;
      return;
    }
  }

  return;
}

#endif
