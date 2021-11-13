#include "forwarder.h"
#include "socks.h"
#include "utils.h"
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#define MAX_ADDRSPEC_ARGS 3
#define LISTEN_BACKLOG 8

// LSB nibble defines number of args
enum conn_mode { CM_NONE = 0, CM_TCP = 0x12, CM_UDS = 0x21, CM_VSOCK = 0x32, CM_VSOCKMULT = 0x43, CM_TCP6 = 0x52 };

static enum conn_mode parse_addrspec(char *arg, const char **outargs);

static void start_server_loop(int svrfd, enum conn_mode cm_cli, const char **cli_args);

static int client_connect(enum conn_mode cm_cli, const char **cli_args);

static void start_client_loop(int fd1, int fd2);

int main(int argc, char **argv) {
  if (argc != 3)
    goto usage;

  const char *svr_args[MAX_ADDRSPEC_ARGS];
  const char *cli_args[MAX_ADDRSPEC_ARGS];

  enum conn_mode cm_svr = parse_addrspec(argv[1], svr_args);
  enum conn_mode cm_cli = parse_addrspec(argv[2], cli_args);
  if (!(cm_svr && cm_cli))
    goto usage;

  int svrfd;
  switch (cm_svr) {
  case CM_TCP:
    svrfd = create_tcp_server(false, svr_args[0], svr_args[1]);
    break;
  case CM_TCP6:
    svrfd = create_tcp_server(true, svr_args[0], svr_args[1]);
    break;
  case CM_UDS:
    svrfd = create_uds_server(svr_args[0]);
    break;
  case CM_VSOCK:
#ifdef __linux__
    // VSOCK is strictly Linux only
    svrfd = create_vsock_server(svr_args[0], svr_args[1]);
#else
    goto usage;
#endif
    break;
  default:
    goto usage;
  }

  if (svrfd < 0)
    err(1, "Error creating socket server");

  listen(svrfd, LISTEN_BACKLOG);
  start_server_loop(svrfd, cm_cli, cli_args);

  return 0;

usage:
  puts("Usage: catsock (listen_addrspec) (connect_addrspec)");
  puts("");
  puts("If a client connect to the socket specified from 'listen_addrspec', "
       "catsock will accept the connection, fork, and connect to 'connect_addrspec', "
       "and performs a bidirectional data forwarding.");
  puts("");
  puts("Available addrspecs:");
  puts(" - TCP:host:port");
  puts(" - TCP6:host:port");
  puts(" - UDS:path");
  puts(" - VSOCK:cid:port (Linux only)");
  puts(" - VSOCKMULT:path:cid:port (connect only)");
  return 0;
}

static enum conn_mode parse_addrspec(char *arg, const char **outargs) {
  char *tok = strsep(&arg, ":");
  if (!tok)
    return CM_NONE;
  enum conn_mode cm = CM_NONE;
  if (strcmp(tok, "TCP") == 0)
    cm = CM_TCP;
  else if (strcmp(tok, "UDS") == 0)
    cm = CM_UDS;
  else if (strcmp(tok, "VSOCK") == 0)
    cm = CM_VSOCK;
  else if (strcmp(tok, "VSOCKMULT") == 0)
    cm = CM_VSOCKMULT;
  else if (strcmp(tok, "TCP6") == 0)
    cm = CM_TCP6;
  else
    return CM_NONE;

  int num_args = cm & 0xf;
  int argidx = 0;
  while (tok) {
    if (argidx > num_args)
      // extraneous argument found
      return CM_NONE;
    tok = strsep(&arg, ":");
    if (tok)
      outargs[argidx++] = tok;
  }

  return cm;
}

static void start_server_loop(int svrfd, enum conn_mode cm_cli, const char **cli_args) {
  for (;;) {
    int commfd = accept(svrfd, NULL, NULL);
    if (commfd < 0) {
      if (errno == EINTR)
        continue;
      err(1, "Socket accept error");
    }

    pid_t pid = fork();
    if (!pid) {
      // wait_debugger();
      // child. make connection to the other end.
      int clifd = client_connect(cm_cli, cli_args);
      if (clifd < 0)
        err(1, "Error connecting to server");
      // does not return
      start_client_loop(commfd, clifd);
    }

    // parent need not deal with commfd again. all child's.
    close(commfd);
    if (pid < 0)
      warn("Fork error");
  }
}

static int client_connect(enum conn_mode cm_cli, const char **cli_args) {
  int clifd = -1;
  switch (cm_cli) {
  case CM_TCP:
    clifd = create_tcp_client(false, cli_args[0], cli_args[1]);
    break;
  case CM_TCP6:
    clifd = create_tcp_client(true, cli_args[0], cli_args[1]);
    break;
  case CM_UDS:
    clifd = create_uds_client(cli_args[0]);
    break;
  case CM_VSOCK:
#ifdef __linux__
    clifd = create_vsock_client(cli_args[0], cli_args[1]);
#else
    warnx("VSOCK only works on Linux!");
    errno = EINVAL;
#endif
    break;
  case CM_VSOCKMULT:
    clifd = create_vsock_mult_client(cli_args[0], cli_args[1], cli_args[2]);
    break;
  default:
    errno = EINVAL;
    warnx("Unknown connection mode.");
    break;
  }
  return clifd;
}

static void start_client_loop(int fd1, int fd2) {
  void *inst = init_forwarder(fd1, fd2);
  if (inst)
    run_forwarder(inst);
  exit(0);
}
