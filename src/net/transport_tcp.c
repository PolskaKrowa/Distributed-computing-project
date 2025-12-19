#include "../../include/transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>

struct transport { int listen_fd; };
struct transport_conn { int fd; char peer[64]; };

static int set_close_exec(int fd) {
    int flags = fcntl(fd, F_GETFD);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static ssize_t read_n(int fd, void *buf, size_t n) {
    size_t offs = 0;
    while (offs < n) {
        ssize_t r = read(fd, (char*)buf + offs, n - offs);
        if (r == 0) return 0; // EOF
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        offs += (size_t)r;
    }
    return (ssize_t)offs;
}

static ssize_t write_n(int fd, const void *buf, size_t n) {
    size_t offs = 0;
    while (offs < n) {
        ssize_t w = write(fd, (const char*)buf + offs, n - offs);
        if (w <= 0) {
            if (w < 0 && errno == EINTR) continue;
            return -1;
        }
        offs += (size_t)w;
    }
    return (ssize_t)offs;
}

int transport_listen(const char *bind_addr, uint16_t port, transport_t **out) {
    (void)bind_addr; // currently ignored, listen on INADDR_ANY
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) return -1;
    int yes = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(lfd, (struct sockaddr*)&a, sizeof(a)) < 0) {
        close(lfd);
        return -1;
    }
    if (listen(lfd, 16) < 0) {
        close(lfd);
        return -1;
    }
    if (set_close_exec(lfd) != 0) {
        close(lfd);
        return -1;
    }
    transport_t *t = malloc(sizeof(*t));
    t->listen_fd = lfd;
    *out = t;
    return 0;
}

int transport_accept(transport_t *t, transport_conn_t **out) {
    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
    int cfd = accept(t->listen_fd, (struct sockaddr*)&peer, &plen);
    if (cfd < 0) return -1;
    if (set_close_exec(cfd) != 0) {
        close(cfd);
        return -1;
    }
    transport_conn_t *c = malloc(sizeof(*c));
    c->fd = cfd;
    inet_ntop(AF_INET, &peer.sin_addr, c->peer, sizeof(c->peer));
    *out = c;
    return 0;
}

int transport_connect(const char *host, uint16_t port, transport_conn_t **out) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr*)&a, sizeof(a)) < 0) {
        close(fd);
        return -1;
    }
    if (set_close_exec(fd) != 0) { close(fd); return -1; }
    transport_conn_t *c = malloc(sizeof(*c));
    c->fd = fd;
    strncpy(c->peer, host, sizeof(c->peer)-1);
    c->peer[sizeof(c->peer)-1] = '\0';
    *out = c;
    return 0;
}

int transport_send_message(transport_conn_t *c, uint32_t type, const void *payload, uint32_t payload_len) {
    // header: uint32_t type, uint32_t len (network byte order)
    uint32_t hdr[2];
    hdr[0] = htonl(type);
    hdr[1] = htonl(payload_len);
    if (write_n(c->fd, hdr, sizeof(hdr)) != sizeof(hdr)) return -1;
    if (payload_len > 0) {
        if (write_n(c->fd, payload, payload_len) != (ssize_t)payload_len) return -1;
    }
    return 0;
}

int transport_recv_message(transport_conn_t *c, uint32_t *out_type, void **out_payload, uint32_t *out_payload_len) {
    uint32_t hdr[2];
    ssize_t r = read_n(c->fd, hdr, sizeof(hdr));
    if (r == 0) return 1; // peer closed
    if (r != sizeof(hdr)) return -1;
    uint32_t type = ntohl(hdr[0]);
    uint32_t len = ntohl(hdr[1]);
    void *buf = NULL;
    if (len > 0) {
        buf = malloc(len + 1);
        if (!buf) return -1;
        if (read_n(c->fd, buf, len) != (ssize_t)len) {
            free(buf);
            return -1;
        }
        ((char*)buf)[len] = '\0';
    }
    *out_type = type;
    *out_payload = buf;
    *out_payload_len = len;
    return 0;
}

int transport_close_conn(transport_conn_t *c) {
    if (!c) return 0;
    close(c->fd);
    free(c);
    return 0;
}

int transport_shutdown(transport_t *t) {
    if (!t) return 0;
    close(t->listen_fd);
    free(t);
    return 0;
}