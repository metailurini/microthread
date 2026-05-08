#include "microthread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
int main(void) {
    puts("echo_server example requires the Unix v0.7 fd/socket backend");
    return 0;
}
#else
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#define READ_TIMEOUT_MS UINT64_C(30000)
#define WRITE_TIMEOUT_MS UINT64_C(30000)

typedef struct client_arg {
    int fd;
} client_arg_t;

static void client_task(void *arg) {
    client_arg_t *client = (client_arg_t *)arg;
    int fd = client->fd;
    free(client);

    char buf[1024];
    for (;;) {
        ssize_t n = mt_net_read(fd, buf, sizeof(buf), READ_TIMEOUT_MS);
        if (n == 0) {
            break;
        }
        if (n < 0) {
            fprintf(stderr, "client read failed: %zd\n", n);
            break;
        }

        ssize_t written = mt_net_write(fd, buf, (size_t)n, WRITE_TIMEOUT_MS);
        if (written != n) {
            fprintf(stderr, "client write failed: %zd\n", written);
            break;
        }
    }

    mt_net_close(fd);
}

static void server_task(void *arg) {
    int listen_fd = *(int *)arg;

    for (;;) {
        int client_fd = mt_net_accept(listen_fd, NULL, NULL, UINT64_C(60000));
        if (client_fd == MT_ERR_TIMEOUT) {
            puts("no clients arrived before timeout; shutting down example");
            break;
        }
        if (client_fd < 0) {
            fprintf(stderr, "accept failed: %d\n", client_fd);
            break;
        }

        client_arg_t *client = (client_arg_t *)malloc(sizeof(*client));
        if (!client) {
            mt_net_close(client_fd);
            break;
        }
        client->fd = client_fd;
        if (mt_go(client_task, client) < 0) {
            free(client);
            mt_net_close(client_fd);
            break;
        }
    }
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1";
    const char *port = argc > 1 ? argv[1] : "8080";

    signal(SIGPIPE, SIG_IGN);

    int listen_fd = mt_net_listen_tcp(host, port, 128);
    if (listen_fd < 0) {
        fprintf(stderr, "listen failed on %s:%s: %d\n", host, port, listen_fd);
        return 1;
    }

    printf("echo server listening on %s:%s\n", host, port);
    puts("try: nc 127.0.0.1 8080");

    mt_init();
    mt_go(server_task, &listen_fd);
    int rc = mt_runtime_start(4);
    mt_net_close(listen_fd);
    mt_shutdown();
    return rc == MT_OK ? 0 : 1;
}
#endif
