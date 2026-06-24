/* serial.c — see serial.h. Pattern lifted from 1984's usifac.c. */

#define _XOPEN_SOURCE 600   /* posix_openpt, grantpt, unlockpt, ptsname */
#define _DEFAULT_SOURCE     /* glibc: cfmakeraw, MSG_NOSIGNAL */
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__) || defined(__NetBSD__)
#define __BSD_VISIBLE 1     /* BSD: cfmakeraw, INADDR_LOOPBACK */
#endif

#include "serial.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#endif

/* MSG_NOSIGNAL is a Linux extension; on the BSDs (and Windows) we
 * suppress SIGPIPE differently — see serial_init(). Fallback must
 * come after <sys/socket.h> so the system header has had its say. */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define MASK (SERIAL_RING - 1)

static inline size_t rb_count(size_t head, size_t tail) {
    return (head - tail) & MASK;
}
static inline size_t rb_space(size_t head, size_t tail) {
    return MASK - rb_count(head, tail);
}
static inline bool rb_empty(size_t head, size_t tail) { return head == tail; }

static void rx_push(Serial *s, u8 b) {
    if (rb_space(s->rx_head, s->rx_tail) == 0) return;
    s->rx_buf[s->rx_head & MASK] = b;
    s->rx_head = (s->rx_head + 1) & MASK;
}
bool serial_rx_pop(Serial *s, u8 *out) {
    if (rb_empty(s->rx_head, s->rx_tail)) return false;
    *out = s->rx_buf[s->rx_tail & MASK];
    s->rx_tail = (s->rx_tail + 1) & MASK;
    return true;
}
bool serial_tx_push(Serial *s, u8 b) {
    if (rb_space(s->tx_head, s->tx_tail) == 0) return false;
    s->tx_buf[s->tx_head & MASK] = b;
    s->tx_head = (s->tx_head + 1) & MASK;
    return true;
}
static bool tx_pop(Serial *s, u8 *out) {
    if (rb_empty(s->tx_head, s->tx_tail)) return false;
    *out = s->tx_buf[s->tx_tail & MASK];
    s->tx_tail = (s->tx_tail + 1) & MASK;
    return true;
}
bool serial_rx_has(const Serial *s) { return !rb_empty(s->rx_head, s->rx_tail); }

#ifndef _WIN32
static int open_pty(Serial *s, const char *link_path) {
    int fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror("serial: posix_openpt"); return -1; }
    if (grantpt(fd) < 0 || unlockpt(fd) < 0) {
        perror("serial: grantpt/unlockpt"); close(fd); return -1;
    }
    const char *name = ptsname(fd);
    if (!name) { close(fd); return -1; }
    snprintf(s->pty_slave, sizeof(s->pty_slave), "%s", name);

    /* Open the slave once and put it in raw mode so the line discipline
     * survives later host-side opens; also keeps the /dev/pts/N node
     * around between user reconnects. See 1984/src/usifac.c open_pty()
     * for the full rationale. */
    int sfd = open(name, O_RDWR | O_NOCTTY);
    if (sfd >= 0) {
        struct termios tio;
        if (tcgetattr(sfd, &tio) == 0) {
            cfmakeraw(&tio);
            tio.c_cflag |= CS8 | CREAD | CLOCAL;
            cfsetispeed(&tio, B9600);
            cfsetospeed(&tio, B9600);
            tcsetattr(sfd, TCSANOW, &tio);
        }
        close(sfd);
    }

    s->pty_master = fd;

    /* Stable host-side alias so the user doesn't have to chase the
     * randomised /dev/pts/N each launch. Replaces any prior link
     * (stale from a crashed previous run is harmless). NULL or empty
     * disables the symlink entirely. */
    if (link_path && link_path[0]) {
        unlink(link_path);
        if (symlink(s->pty_slave, link_path) == 0)
            snprintf(s->pty_link, sizeof(s->pty_link), "%s", link_path);
        else
            s->pty_link[0] = '\0';
    } else {
        s->pty_link[0] = '\0';
    }

    if (s->pty_link[0])
        fprintf(stderr, "serial: PTY ready at %s (alias %s)\n",
                s->pty_slave, s->pty_link);
    else
        fprintf(stderr, "serial: PTY ready at %s\n", s->pty_slave);
    return 0;
}

static int open_tcp(Serial *s, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("serial: socket"); return -1; }

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        fprintf(stderr, "serial: bind localhost:%d failed: %s\n", port, strerror(errno));
        close(fd); return -1;
    }
    if (listen(fd, 1) < 0) { perror("serial: listen"); close(fd); return -1; }

    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    s->tcp_listen = fd;
    s->tcp_port   = port;
    fprintf(stderr, "serial: TCP listening on localhost:%d\n", port);
    return 0;
}

static void close_fd(int *pfd) {
    if (*pfd >= 0) { close(*pfd); *pfd = -1; }
}
#endif /* !_WIN32 */

void serial_init(Serial *s, bool enable, const char *backend, int tcp_port,
                 const char *pty_link_path) {
    memset(s, 0, sizeof(*s));
    s->pty_master = -1;
    s->tcp_listen = -1;
    s->tcp_client = -1;
    s->present    = enable;
    if (!s->present) return;

#ifdef _WIN32
    (void)backend; (void)tcp_port; (void)pty_link_path;
    fprintf(stderr, "serial: backend not supported on Windows yet — disabling\n");
    s->present = false;
    return;
#else
    /* On the BSDs MSG_NOSIGNAL is a no-op (set to 0 above); ignore
     * SIGPIPE process-wide so a TCP peer hanging up mid-send can't
     * terminate the emulator. Harmless on Linux too. */
    signal(SIGPIPE, SIG_IGN);

    if (backend && !strcmp(backend, "tcp")) {
        s->backend = SERIAL_BACKEND_TCP;
        if (open_tcp(s, tcp_port) < 0) s->present = false;
    } else {
        s->backend = SERIAL_BACKEND_PTY;
        if (open_pty(s, pty_link_path) < 0) s->present = false;
    }
#endif
}

void serial_shutdown(Serial *s) {
#ifndef _WIN32
    close_fd(&s->pty_master);
    close_fd(&s->tcp_client);
    close_fd(&s->tcp_listen);
    if (s->pty_link[0]) {
        unlink(s->pty_link);
        s->pty_link[0] = '\0';
    }
#endif
    s->present = false;
}

#ifndef _WIN32
static void poll_pty(Serial *s) {
    if (s->pty_master < 0) return;
    while (rb_space(s->rx_head, s->rx_tail) > 0) {
        u8 c;
        ssize_t n = read(s->pty_master, &c, 1);
        if (n <= 0) break;
        rx_push(s, c);
    }
    u8 c;
    while (tx_pop(s, &c)) {
        ssize_t n = write(s->pty_master, &c, 1);
        if (n < 0) break;
    }
}

static void poll_tcp(Serial *s) {
    if (s->tcp_listen < 0) return;

    if (s->tcp_client < 0) {
        int fd = accept(s->tcp_listen, NULL, NULL);
        if (fd >= 0) {
            int fl = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            s->tcp_client = fd;
            fprintf(stderr, "serial: TCP client connected\n");
        }
    }
    if (s->tcp_client < 0) return;

    bool read_eof = false;
    while (!read_eof && rb_space(s->rx_head, s->rx_tail) > 0) {
        u8 c;
        ssize_t n = recv(s->tcp_client, &c, 1, 0);
        if (n == 0) { read_eof = true; break; }
        if (n < 0) break;
        rx_push(s, c);
    }
    u8 c;
    while (tx_pop(s, &c)) {
        ssize_t n = send(s->tcp_client, &c, 1, MSG_NOSIGNAL);
        if (n < 0) {
            close(s->tcp_client);
            s->tcp_client = -1;
            fprintf(stderr, "serial: TCP client disconnected\n");
            return;
        }
    }
}
#endif /* !_WIN32 */

void serial_poll(Serial *s) {
    if (!s->present) return;
#ifndef _WIN32
    if (s->backend == SERIAL_BACKEND_PTY) poll_pty(s);
    else                                  poll_tcp(s);
#else
    (void)s;
#endif
}
