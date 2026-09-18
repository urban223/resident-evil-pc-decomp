// net.cpp - Windows UDP sockets behind plat_net_* (Phase 5, RAID co-op).
//
// One of the few files allowed to include a Windows header directly; see
// platform.cpp's note. src/game never sees a SOCKET, a sockaddr or a WSA call -
// tests/check_platform_boundary.py enforces that for networking as well as for
// Win32, because socket() and recv() are spelled the same on both platforms.
//
// Winsock needs an explicit startup, which POSIX does not. Rather than add a
// plat_net_init() to the interface for one platform's benefit, it is done
// lazily on the first open and torn down when the last socket closes - so the
// shared header stays the same shape as the Linux side.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>

// Not declared by every SDK/header order; this is the documented value.
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#include "../platform.h"
#include "../../DebugPrint.h"

#pragma comment(lib, "ws2_32.lib")

static int s_open = 0;      // live sockets, for the lazy WSA startup/cleanup

static int wsa_up(void)
{
    if (s_open > 0) return 1;
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
}

int plat_net_open(unsigned short bindPort)
{
    if (!wsa_up()) return PLAT_NET_INVALID;

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        if (s_open == 0) WSACleanup();
        return PLAT_NET_INVALID;
    }

    // Stop Windows turning an ICMP port-unreachable into a permanent read
    // error on this UDP socket. Without this, a datagram sent to a port with
    // nothing bound - which happens every time one side reaches RAID before
    // the other - makes EVERY later recvfrom fail with WSAECONNRESET (10054),
    // not just the next one. The link then looks alive from the sending end
    // and is stone dead at the receiving one: sends keep succeeding, the peer
    // address is right, and no snapshot ever arrives again. That is exactly
    // what the client's log showed - "recv error 10054 x100" and a receive
    // counter frozen at zero while the host happily sent.
    //
    // Windows-only. The Linux side reports ECONNREFUSED only on a CONNECTED
    // UDP socket, and these are unconnected.
    {
        DWORD  in = FALSE;          // FALSE = do not report connection reset
        DWORD  out = 0;
        WSAIoctl(s, SIO_UDP_CONNRESET, &in, sizeof(in), NULL, 0, &out, NULL, NULL);
        // Deliberately unchecked: it is absent on some stacks, and the socket
        // is still usable without it - just fragile in the way described above.
    }

    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(bindPort);
    if (bind(s, (sockaddr*)&sa, sizeof(sa)) == SOCKET_ERROR) {
        closesocket(s);
        if (s_open == 0) WSACleanup();
        return PLAT_NET_INVALID;
    }

    // Non-blocking, because this is polled from inside the 30 Hz tick and a
    // stall here is a dropped frame.
    u_long nb = 1;
    if (ioctlsocket(s, FIONBIO, &nb) == SOCKET_ERROR) {
        closesocket(s);
        if (s_open == 0) WSACleanup();
        return PLAT_NET_INVALID;
    }

    // What port the OS actually gave us. A client passes 0 and never knew its
    // own port, so "the host is sending to the right place" was an assumption
    // rather than a measurement.
    {
        sockaddr_in bound;
        int blen = (int)sizeof(bound);
        if (getsockname(s, (sockaddr*)&bound, &blen) == 0) {
            dbg_printf("[net] socket bound to port %u\n",
                       (unsigned int)ntohs(bound.sin_port));
        }
    }

    s_open++;
    return (int)s;
}

void plat_net_close(int sock)
{
    if (sock == PLAT_NET_INVALID) return;
    closesocket((SOCKET)sock);
    if (--s_open <= 0) {
        s_open = 0;
        WSACleanup();
    }
}

int plat_net_send(int sock, const PlatNetAddr* to, const void* data, int len)
{
    if (sock == PLAT_NET_INVALID || to == 0 || data == 0 || len <= 0) return -1;

    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(to->addr);
    sa.sin_port = htons(to->port);

    int n = sendto((SOCKET)sock, (const char*)data, len, 0,
                   (const sockaddr*)&sa, sizeof(sa));
    if (n == SOCKET_ERROR) {
        static int lastErr = 0;
        const int e = WSAGetLastError();
        if (e != lastErr) { lastErr = e; dbg_printf("[net] send error %d\n", e); }
        return -1;
    }
    return n;
}

int plat_net_recv(int sock, PlatNetAddr* from, void* data, int cap)
{
    if (sock == PLAT_NET_INVALID || data == 0 || cap <= 0) return -1;

    sockaddr_in sa;
    int salen = (int)sizeof(sa);
    int n = recvfrom((SOCKET)sock, (char*)data, cap, 0, (sockaddr*)&sa, &salen);
    if (n == SOCKET_ERROR) {
        // Nothing queued is the normal case on a non-blocking socket, and so is
        // a peer that has gone away on Windows (an ICMP port-unreachable comes
        // back as WSAECONNRESET on the NEXT recvfrom, not as an error on send).
        // Neither is a failure worth telling the caller about.
        const int e = WSAGetLastError();
        // Everything except "nothing queued" is worth naming once. A swallowed
        // WSAECONNRESET and an empty queue both return 0, which made a dead
        // link indistinguishable from an idle one in the co-op counters.
        if (e != WSAEWOULDBLOCK) {
            static int lastErr = 0;
            static unsigned int repeats = 0;
            if (e != lastErr) {
                lastErr = e; repeats = 0;
                dbg_printf("[net] recv error %d\n", e);
            } else if (++repeats % 100 == 0) {
                dbg_printf("[net] recv error %d x%u\n", e, repeats);
            }
        }
        if (e == WSAEWOULDBLOCK || e == WSAECONNRESET) return 0;
        return -1;
    }

    if (from != 0) {
        from->addr = ntohl(sa.sin_addr.s_addr);
        from->port = ntohs(sa.sin_port);
        from->pad  = 0;
    }
    return n;
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

    in_addr a;
    if (inet_pton(AF_INET, host, &a) != 1) return 0;

    out->addr = ntohl(a.s_addr);
    out->port = (unsigned short)port;
    out->pad  = 0;
    return 1;
}
