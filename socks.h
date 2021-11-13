#pragma once

#include "common.h"
#include <stdbool.h>

int create_tcp_server(bool ipv6, const char *host, const char *port);

int create_tcp_client(bool ipv6, const char *host, const char *port);

int create_uds_server(const char *path);

int create_uds_client(const char *path);

#ifdef __linux__

int create_vsock_server(const char *cid, const char *port);

int create_vsock_client(const char *cid, const char *port);

#endif

int create_vsock_mult_client(const char *path, const char *cid, const char *port);
