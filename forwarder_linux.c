// linux only, using the `splice` syscall
#ifdef __linux__

#include "utils.h"
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <err.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

#define MTU (1024 * 1024)

struct io_info {
  size_t filled;
  size_t written;
  bool srcclose;
  bool dstclose;
};

struct fwd_inst {
  int fd1;
  int fd2;
  // pipes: [0] = read, [1] = write
  int pp1[2]; // 1 -> 2
  int pp2[2]; // 2 -> 1
  struct io_info inf12;
  struct io_info inf21;
};

void *init_forwarder(int fd1, int fd2) {
  struct fwd_inst *inst = calloc(sizeof(struct fwd_inst), 1);
  if (!inst)
    err(1, "Error allocating forwarder instance");

  inst->fd1 = fd1;
  inst->fd2 = fd2;

  if (pipe2(inst->pp1, O_NONBLOCK) < 0)
    err(1, "Error creating pipe 1to2");
  if (pipe2(inst->pp2, O_NONBLOCK) < 0)
    err(1, "Error creating pipe 2to1");

  set_fd_flags(fd1, true, O_NONBLOCK);
  set_fd_flags(fd2, true, O_NONBLOCK);

  return inst;
}

void run_forwarder(void *inst) {
  struct fwd_inst *self = inst;

  for(;;) {
    struct pollfd pfds[2];
    int pfdslen = 0;

    if (!self->inf12.filled && !self->inf12.srcclose) {
      pfds[pfdslen].fd = self->fd1;
      pfds[pfdslen++].events = POLLIN;
    } else if (self->inf12.filled && !self->inf12.dstclose) {
      pfds[pfdslen].fd = self->fd2;
      pfds[pfdslen++].events = POLLOUT;
    }
    if (!self->inf21.filled && !self->inf21.srcclose) {
      pfds[pfdslen].fd = self->fd2;
      pfds[pfdslen++].events = POLLIN;
    } else if (self->inf21.filled && !self->inf21.dstclose) {
      pfds[pfdslen].fd = self->fd1;
      pfds[pfdslen++].events = POLLOUT;
    }

    if (!pfdslen)
      exit(0);

    if (poll(pfds, pfdslen, -1) < 0) {
      if (errno == EINTR)
        continue;
      err(1, "poll error");
    }

    for(int i=0; i<pfdslen; ++i) {
      int srcfd, dstfd, opfd;
      struct io_info * actinf;
      bool isout = false;
      size_t splicelen;
      if (pfds[i].revents & POLLIN) {
        srcfd = pfds[i].fd;
        dstfd = srcfd == self->fd1 ? self->pp1[1] : self->pp2[1];
        actinf = srcfd == self->fd1 ? &self->inf12 : &self->inf21;
        opfd = srcfd == self->fd1 ? self->fd2 : self->fd1;
        splicelen = MTU;
      } else if (pfds[i].revents & POLLOUT) {
        dstfd = pfds[i].fd;
        srcfd = dstfd == self->fd1 ? self->pp2[0] : self->pp1[0];
        actinf = dstfd == self->fd1 ? &self->inf21 : &self->inf12;
        opfd = dstfd == self->fd1 ? self->fd2 : self->fd1;
        isout = true;
        splicelen = actinf->filled - actinf->written;
      } else
        continue;

      ssize_t acted = splice(srcfd, NULL, dstfd, NULL, splicelen, SPLICE_F_NONBLOCK);
      if (acted < 0) {
        if (errno == EAGAIN)
          continue;
        err(1, "splice error");
      }
      if (!isout) {
        // in/read
        if (acted == 0) {
          shutdown(opfd, SHUT_RD);
          actinf->srcclose = true;
        }
        actinf->written = 0;
        actinf->filled += acted;
      } else {
        // out/write
        if (acted == 0) {
          shutdown(opfd, SHUT_RDWR);
          actinf->srcclose = actinf->dstclose = true;
        }
        actinf->written += acted;
        if (actinf->written == actinf->filled)
          actinf->filled = 0;
      }
    }
  }
}

#endif
