#include "socks.h"
#include "utils.h"
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/vm_sockets.h>
#endif

static uint32_t parse_uint32(const char *v) {
  // return 0 if invalid
  uint32_t ret;
  int cnt = sscanf(v, "%u", &ret);
  if (!cnt)
    return 0;
  return ret;
}

int create_tcp_server(bool ipv6, const char *host, const char *port) {
  int st;

  if (!(host && port)) {
    warnx("Please specify address and port!");
    errno = EINVAL;
    return -1;
  }

  struct addrinfo addrhints;
  struct addrinfo *addrres;

  memset(&addrhints, 0, sizeof(addrhints));
  addrhints.ai_family = ipv6 ? AF_INET6 : AF_INET;
  addrhints.ai_socktype = SOCK_STREAM;

  st = getaddrinfo(host, port, &addrhints, &addrres);
  if (st < 0) {
    warn("getaddrinfo error");
    return -1;
  }

  for (struct addrinfo *res = addrres; res; res = res->ai_next) {
    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0) {
      warn("Error creating socket");
      continue;
    }

    int val = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    if (bind(s, res->ai_addr, res->ai_addrlen) < 0) {
      warn("Error binding socket");
      goto errcont;
    }

    return s;

  errcont:
    close(s);
    continue;
  }

  errno = EIO;
  return -1;
}

int create_tcp_client(bool ipv6, const char *host, const char *port) {
  int st;

  struct addrinfo addrhints;
  struct addrinfo *addrres;

  if (!(host && port)) {
    warnx("Please specify address and port!");
    errno = EINVAL;
    return -1;
  }

  memset(&addrhints, 0, sizeof(addrhints));
  addrhints.ai_family = ipv6 ? AF_INET6 : AF_INET;
  addrhints.ai_socktype = SOCK_STREAM;

  st = getaddrinfo(host, port, &addrhints, &addrres);
  if (st < 0)
    return -1;

  int s = -1;
  for (struct addrinfo *res = addrres; res; res = res->ai_next) {
    s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0)
      continue;

    if (connect(s, res->ai_addr, res->ai_addrlen) < 0) {
      close(s);
      s = -1;
      continue;
    }

    // connect OK
    break;
  }
  return s;
}

int create_uds_server(const char *path) {
  if (!path) {
    warnx("Please specify socket path!");
    errno = EINVAL;
    return -1;
  }

  int s = socket(AF_UNIX, SOCK_STREAM, 0);
  if (s < 0) {
    warn("Error creating Unix socket");
    return -1;
  }

  struct sockaddr_un addr;
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path, sizeof(addr.sun_path));

  if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    warn("Error binding socket");
    return -1;
  }

  return s;
}

int create_uds_client(const char *path) {
  if (!path) {
    warnx("Please specify socket path!");
    errno = EINVAL;
    return -1;
  }

  int s = socket(AF_UNIX, SOCK_STREAM, 0);
  if (s < 0)
    return -1;

  struct sockaddr_un addr;
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path, sizeof(addr.sun_path));

  if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(s);
    return -1;
  }

  return s;
}

#ifdef __linux

int create_vsock_server(const char *s_cid, const char *s_port) {
  if (!(s_cid && s_port)) {
    warnx("Please specify CID and port number!");
    errno = EINVAL;
    return -1;
  }

  uint32_t cid = parse_uint32(s_cid);
  uint32_t port = parse_uint32(s_port);
  if (!(cid && port)) {
    errno = EINVAL;
    warnx("Invalid parameter");
    return -1;
  }

  int s = socket(AF_VSOCK, SOCK_STREAM, 0);
  if (s < 0) {
    warn("Error creating VSOCK");
    return -1;
  }

  struct sockaddr_vm addr = {0};
  addr.svm_family = AF_VSOCK;
  addr.svm_cid = cid;
  addr.svm_port = port;

  if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    warn("Error binding socket");
    return -1;
  }

  return s;
}

int create_vsock_client(const char *s_cid, const char *s_port) {
  if (!(s_cid && s_port)) {
    warnx("Please specify CID and port number!");
    errno = EINVAL;
    return -1;
  }

  uint32_t cid = parse_uint32(s_cid);
  uint32_t port = parse_uint32(s_port);
  if (!(cid && port)) {
    warnx("Invalid parameter");
    return -1;
  }

  int s = socket(AF_VSOCK, SOCK_STREAM, 0);
  if (s < 0)
    return -1;

  struct sockaddr_vm addr = {0};
  addr.svm_family = AF_VSOCK;
  addr.svm_cid = cid;
  addr.svm_port = port;

  if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(s);
    return -1;
  }

  return s;
}

#endif

int create_vsock_mult_client(const char *path, const char *s_cid, const char *s_port) {
  if (!(path && s_cid && s_port)) {
    warnx("Please specify multiplexer path, CID, and port number!");
    goto err_inval;
  }

  uint32_t cid = parse_uint32(s_cid);
  uint32_t port = parse_uint32(s_port);
  if (!(cid && port)) {
    warnx("Invalid parameter");
    goto err_inval;
  }

  int s = create_uds_client(path);
  if (s < 0)
    return -1;

  char preamble[8 + 1 + 8 + 1 + 1] = {0}; // "%08x.%08x\n" + \0 (cid, port)
  int preamblelen = sprintf(preamble, "%08x.%08x\n", cid, port);
  if (!write_all(s, preamble, preamblelen)) {
    close(s);
    return -1;
  }

  return s;

err_inval:
  errno = EINVAL;
  return -1;
}
