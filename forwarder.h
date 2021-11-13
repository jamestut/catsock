#pragma once

#include <sys/types.h>

void *init_forwarder(int fd1, int fd2);

void run_forwarder(void *inst);
