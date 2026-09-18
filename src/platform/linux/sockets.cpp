// net.cpp - Linux UDP sockets behind plat_net_* (Phase 5, RAID co-op).
//
// The POSIX half of the seam declared in ../platform.h. Simpler than the
// Windows side in exactly one way: no WSAStartup, so no lazy init and no
// refcount - which is why that bookkeeping was kept on the Windows side rather
// than pushed into the shared interface as a plat_net_init() both platforms
// would have had to carry for one platform's benefit.
//
// No new link library: sockets are in libc.
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "../platform.h"

int plat_net_open(unsigned short bindPort)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return PLAT_NET_INVALID;

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(bindPort);
    if (bind(s, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        close(s);
        return PLAT_NET_INVALID;
    }

    // Non-blocking: this is polled from inside the 30 Hz tick.
    int fl = fcntl(s, F_GETFL, 0);
    if (fl < 0 || fcntl(s, F_SETFL, fl | O_NONBLOCK) < 0) {
        close(s);
        return PLAT_NET_INVALID;
    }

    return s;
}

void plat_net_close(int sock)
{
    if (sock == PLAT_NET_INVALID) return;
    close(sock);
}

int plat_net_send(int sock, const PlatNetAddr* to, const void* data, int len)
{
    if (sock == PLAT_NET_INVALID || to == 0 || data == 0 || len <= 0) return -1;

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(to->addr);
    sa.sin_port = htons(to->port);

    ssize_t n = sendto(sock, data, (size_t)len, 0,
                       (const struct sockaddr*)&sa, sizeof(sa));
    return (n < 0) ? -1 : (int)n;
}

int plat_net_recv(int sock, PlatNetAddr* from, void* data, int cap)
{
    if (sock == PLAT_NET_INVALID || data == 0 || cap <= 0) return -1;

    struct sockaddr_in sa;
    socklen_t salen = (socklen_t)sizeof(sa);
    ssize_t n = recvfrom(sock, data, (size_t)cap, 0,
                         (struct sockaddr*)&sa, &salen);
    if (n < 0) {
        // Nothing queued is the normal case on a non-blocking socket. ECONNREFUSED
        // is the POSIX counterpart of the Windows WSAECONNRESET note: an ICMP
        // port-unreachable from a peer that has gone away surfaces on the next
        // receive, and is not a failure of this socket.
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED) return 0;
        if (errno == EINTR) return 0;
        return -1;
    }

    if (from != 0) {
        from->addr = ntohl(sa.sin_addr.s_addr);
        from->port = ntohs(sa.sin_port);
        from->pad  = 0;
    }
    return (int)n;
}

int plat_net_parse(const char* text, PlatNetAddr* out)
{
    if (text == 0 || out == 0) return 0;

    char host[64];
    int i = 0;
    while (text[i] != 0 && text[i] != ':' && i < (int)sizeof(host) - 1) {
        host[i] = text[i];
        i++;
    }
    host[i] = 0;
    if (text[i] != ':') return 0;

    int port = 0;
    for (const char* p = text + i + 1; *p != 0; p++) {
        if (*p < '0' || *p > '9') return 0;
        port = port * 10 + (*p - '0');
        if (port > 65535) return 0;
    }
    if (port <= 0) return 0;

    struct in_addr a;
    if (inet_pton(AF_INET, host, &a) != 1) return 0;

    out->addr = ntohl(a.s_addr);
    out->port = (unsigned short)port;
    out->pad  = 0;
    return 1;
}
