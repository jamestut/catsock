#pragma once

#include "common.h"
#include <stdbool.h>

int set_fd_flags(int fd, bool set, int flags);

// WARNING! this function is NOT thread safe!
bool write_all(int fd, const void *buff, UINT len);

bool read_all(int fd, const void *buff, UINT len);

void wait_debugger();
