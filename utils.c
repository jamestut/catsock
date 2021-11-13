#include "utils.h"
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

bool rw_all(bool iswrite, int fd, const void *buff, UINT len);

int set_fd_flags(int fd, bool set, int flags) {
  int fdflags = fcntl(fd, F_GETFL, 0);
  if (set)
    fdflags |= flags;
  else
    fdflags &= ~flags;
  return fcntl(fd, F_SETFL, fdflags);
}

bool write_all(int fd, const void *buff, UINT len) { return rw_all(true, fd, buff, len); }

bool read_all(int fd, const void *buff, UINT len) { return rw_all(false, fd, buff, len); }

bool rw_all(bool iswrite, int fd, const void *buff, UINT len) {
  // caller should not attempt to do this with len == 0
  // if they did so, they must have forgotten to do EOF checks or things like that
  assert(len);

  int done = 0;
  while (done < len) {
    int currdone = iswrite ? write(fd, (void *)((uintptr_t)buff + done), len - done)
                           : read(fd, (void *)((uintptr_t)buff + done), len - done);
    if (currdone < 0) {
      if ((errno == EAGAIN) || (errno == EINTR))
        continue;
      return false;
    }
    if (currdone == 0) {
      // EOF reached (?) but we haven't written everything
      errno = EIO;
      return false;
    }
    done += currdone;
  }

  return true;
}

void wait_debugger() {
  printf("Please attach debugger to PID %d\n", getpid());
  bool stop = false;
  while (!stop)
    sleep(1);
}
