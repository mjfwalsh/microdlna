/* MicroDLNA media server
 * This file is part of MicroDLNA.
 *
 * The code herein is based on the MiniUPnP Project.
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 *
 * Copyright (c) 2016, Gabor Simon
 * All rights reserved.
 *
 * Based on the MiniDLNA project:
 * Copyright (c) 2006, Thomas Bernard
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * The name of the author may not be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <strings.h>

#include "microdlnapath.h"
#include "getifaddr.h"
#include "log.h"
#include "minissdp.h"
#include "globalvars.h"
#include "upnphttp.h"
#include "utils.h"

/* SSDP ip/port */
static const int ssdp_port = 1900;
static const char *ssdp_mcast_addr = "239.255.255.250";

static int add_multicast_membership(int s, int ifindex)
{
    struct ip_mreqn imr;        /* Ip multicast membership */

    /* setting up imr structure */
    memset(&imr, '\0', sizeof(imr));
    imr.imr_multiaddr.s_addr = inet_addr(ssdp_mcast_addr);
    imr.imr_ifindex = ifindex;

    /* Setting the socket options will guarantee, tha we will only receive
     * multicast traffic on a specific Interface.
     * In addition the kernel is instructed to send an igmp message (choose
     * mcast group) on the specific interface/subnet. */
    int ret = setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)&imr, sizeof(imr));
    if (ret < 0 && errno != EADDRINUSE)
    {
        PRINT_LOG(E_ERROR, "setsockopt(udp, IP_ADD_MEMBERSHIP): %d\n", errno);
        return -1;
    }

    return 0;
}

/* Open and configure the socket listening for
 * SSDP udp packets sent on 239.255.255.250 port 1900 */
int open_ssdp_receive_socket(void)
{
    struct sockaddr_in sockname;

    int s = socket(PF_INET, SOCK_DGRAM, 0);

    if (s < 0)
    {
        PRINT_LOG(E_ERROR, "socket(udp): %d\n", errno);
        return -1;
    }

    int i = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) < 0)
        PRINT_LOG(E_ERROR, "setsockopt(udp, SO_REUSEADDR): %d\n", errno);
#ifdef __linux__
    if (setsockopt(s, IPPROTO_IP, IP_PKTINFO, &i, sizeof(i)) < 0)
        PRINT_LOG(E_ERROR, "setsockopt(udp, IP_PKTINFO): %d\n", errno);
#endif
    memset(&sockname, 0, sizeof(struct sockaddr_in));
    sockname.sin_family = AF_INET;
    sockname.sin_port = htons(ssdp_port);
#ifdef __linux__
    /* NOTE: Binding a socket to a UDP multicast address means, that we just want
     * to receive datagramms send to this multicast address.
     * To specify the local nics we want to use we have to use setsockopt,
     * see AddMulticastMembership(...). */
    sockname.sin_addr.s_addr = inet_addr(ssdp_mcast_addr);
#else
    /* NOTE: Binding to ssdp_mcast_addr on Darwin & *BSD causes NOTIFY replies are
     * sent from ssdp_mcast_addr what forces some clients to ignore subsequent
     * unsolicited NOTIFY packets from the real interface address. */
    sockname.sin_addr.s_addr = htonl(INADDR_ANY);
#endif

    if (bind(s, (struct sockaddr *)&sockname, sizeof(struct sockaddr_in)) < 0)
    {
        PRINT_LOG(E_ERROR, "bind(udp): %d\n", errno);
        close(s);
        return -1;
    }

    return s;
}

/* open the UDP socket used to send SSDP notifications to
 * the multicast group reserved for them */
int open_ssdp_notify_socket(struct lan_addr_s *iface, int sssdp)
{
    int s = socket(PF_INET, SOCK_DGRAM, 0);

    if (s < 0)
    {
        PRINT_LOG(E_ERROR, "socket(udp_notify): %d\n", errno);
        return -1;
    }

    struct in_addr mc_if;
    mc_if.s_addr = iface->addr.s_addr;

    unsigned char loopchar = 0;
    if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&loopchar, sizeof(loopchar))
        < 0)
    {
        PRINT_LOG(E_ERROR, "setsockopt(udp_notify, IP_MULTICAST_LOOP): %d\n", errno);
        close(s);
        return -1;
    }

    if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (char *)&mc_if, sizeof(mc_if)) < 0)
    {
        PRINT_LOG(E_ERROR, "setsockopt(udp_notify, IP_MULTICAST_IF): %d\n", errno);
        close(s);
        return -1;
    }

    uint8_t ttl = 4;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    int bcast = 1;
    if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast)) < 0)
    {
        PRINT_LOG(E_ERROR, "setsockopt(udp_notify, SO_BROADCAST): %d\n", errno);
        close(s);
        return -1;
    }

    struct sockaddr_in sockname;
    memset(&sockname, 0, sizeof(struct sockaddr_in));
    sockname.sin_family = AF_INET;
    sockname.sin_addr.s_addr = iface->addr.s_addr;

    if (bind(s, (struct sockaddr *)&sockname, sizeof(struct sockaddr_in)) < 0)
    {
        PRINT_LOG(E_ERROR, "bind(udp_notify): %d\n", errno);
        close(s);
        return -1;
    }

    if (add_multicast_membership(sssdp, iface->ifindex) < 0)
    {
        PRINT_LOG(E_ERROR, "Failed to add multicast membership for address %s\n",
                  iface->str);
    }

    return s;
}

static const char *const known_service_types[] = {
    uuidvalue,
    "upnp:rootdevice",
    "urn:schemas-upnp-org:device:MediaServer:",
    "urn:schemas-upnp-org:service:ContentDirectory:",
    "urn:schemas-upnp-org:service:ConnectionManager:",
    "urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:",
    0
};

static void microsleep(long msecs)
{
    struct timespec sleep_time;

    sleep_time.tv_sec = 0;
    sleep_time.tv_nsec = msecs * 1000;
    nanosleep(&sleep_time, NULL);
}

/* not really an SSDP "announce" as it is the response
 * to a SSDP "M-SEARCH" */
static void send_ssdp_response(int s, struct sockaddr_in sockname, int st_no,
                               const char *host)
{
    char buf[400];
    char tmstr[30];
    time_t tm = time(NULL);
    struct tm tbuf;

    /*
     * follow guideline from document "UPnP Device Architecture 1.0"
     * uppercase is recommended.
     * DATE: is recommended
     * SERVER: OS/ver UPnP/1.0 microdlna/1.0
     * - check what to put in the 'Cache-Control' header
     * */
    strftime(tmstr, sizeof(tmstr), "%a, %d %b %Y %H:%M:%S GMT", gmtime_r(&tm, &tbuf));
    int l = snprintf(buf, sizeof(buf), "HTTP/1.1 200 OK\r\n"
                     "CACHE-CONTROL: max-age=%u\r\n"
                     "DATE: %s\r\n"
                     "ST: %s%s\r\n"
                     "USN: %s%s%s%s\r\n"
                     "EXT:\r\n"
                     "SERVER: " MICRODLNA_SERVER_STRING "\r\n"
                     "LOCATION: http://%s:%u" ROOTDESC_PATH "\r\n"
                     "Content-Length: 0\r\n"
                     "\r\n",
                     (((unsigned)notify_interval) << 1) + 10,
                     tmstr,
                     known_service_types[st_no],
                     (st_no > 1 ? "1" : ""),
                     uuidvalue,
                     (st_no > 0 ? "::" : ""),
                     (st_no == 0 ? "" : known_service_types[st_no]),
                     (st_no > 1 ? "1" : ""), host, (unsigned int)listening_port);
    PRINT_LOG(E_DEBUG, "Sending M-SEARCH response to %s:%d ST: %s\n",
              inet_ntoa_ts(sockname.sin_addr), ntohs(sockname.sin_port),
              known_service_types[st_no]);
    int n =
        sendto(s, buf, l, 0, (struct sockaddr *)&sockname, sizeof(struct sockaddr_in));
    if (n < 0)
        PRINT_LOG(E_ERROR, "sendto(udp): %d\n", errno);
}

void send_ssdp_notifies(int s, const char *host)
{
    struct sockaddr_in sockname;
    char bufr[400];

    memset(&sockname, 0, sizeof(struct sockaddr_in));
    sockname.sin_family = AF_INET;
    sockname.sin_port = htons(ssdp_port);
    sockname.sin_addr.s_addr = inet_addr(ssdp_mcast_addr);

    for (int dup = 0; dup < 2; dup++)
    {
        if (dup)
            microsleep(200000);

        for (int i = 0; known_service_types[i]; i++)
        {
            int l = snprintf(bufr, sizeof(bufr),
                             "NOTIFY * HTTP/1.1\r\n"
                             "HOST:%s:%d\r\n"
                             "CACHE-CONTROL:max-age=%u\r\n"
                             "LOCATION:http://%s:%d" ROOTDESC_PATH "\r\n"
                             "SERVER: " MICRODLNA_SERVER_STRING "\r\n"
                             "NT:%s%s\r\n"
                             "USN:%s%s%s%s\r\n"
                             "NTS:ssdp:alive\r\n"
                             "\r\n",
                             ssdp_mcast_addr, ssdp_port,
                             ((unsigned int)notify_interval << 1) + 10,
                             host, listening_port,
                             known_service_types[i],
                             (i > 1 ? "1" : ""),
                             uuidvalue,
                             (i > 0 ? "::" : ""),
                             (i > 0 ? known_service_types[i] : ""),
                             (i > 1 ? "1" : ""));
            if (l >= sizeof(bufr))
            {
                PRINT_LOG(E_ERROR, "SendSSDPNotifies(): truncated output\n");
                l = sizeof(bufr);
            }
            PRINT_LOG(E_DEBUG, "Sending ssdp:alive [%d]\n", s);
            int n = sendto(s, bufr, l, 0, (struct sockaddr *)&sockname,
                           sizeof(struct sockaddr_in));
            if (n < 0)
                PRINT_LOG(E_ERROR, "sendto(udp_notify=%d, %s): %d\n", s, host, errno);
        }
    }
}

/* ProcessSSDPRequest()
 * process SSDP M-SEARCH requests and responds to them */
void process_ssdp_request(int sssdp)
{
    char bufr[200];
    struct sockaddr_in sendername;
    int mx_val = -1;
    char *st = NULL, *mx = NULL, *man = NULL;

#ifdef __linux__
    char cmbuf[CMSG_SPACE(sizeof(struct in_pktinfo))];
    struct iovec iovec = {
        .iov_base = bufr,
        .iov_len = sizeof(bufr) - 1
    };
    struct msghdr mh = {
        .msg_name = &sendername,
        .msg_namelen = sizeof(struct sockaddr_in),
        .msg_iov = &iovec,
        .msg_iovlen = 1,
        .msg_control = cmbuf,
        .msg_controllen = sizeof(cmbuf)
    };

    int n = recvmsg(sssdp, &mh, 0);
#else
    socklen_t len_r = sizeof(struct sockaddr_in);

    int n = recvfrom(sssdp, bufr, sizeof(bufr) - 1, 0,
                     (struct sockaddr *)&sendername, &len_r);
#endif

    if (n < 0)
    {
        PRINT_LOG(E_ERROR, "recvfrom(udp): %d\n", errno);
        return;
    }
    if (n >= sizeof(bufr))
    {
        PRINT_LOG(E_ERROR, "recvfrom(udp): exceeded buffer\n");
        return;
    }

    bufr[n] = '\0';

    char *p = bufr;
    if (strncmp(p, "M-SEARCH * HTTP/1.1\r\n", 21) != 0)
        return;
    p += 21;

    for (;;)
    {
        char *name = p;

        // find the end of the name (or end of the headers)
        while (*p != ':' && *p != '\0' && *p != '\r')
            p++;
        if (*p != ':' || p == name)
            break;

        // make name null terminated
        *p++ = '\0';

        // skip over any white space to the value
        while (*p == ' ')
            p++;
        char *value = p;

        // find the end of the line
        while (*p != '\0' && *p != '\r')
            p++;
        if (*p != '\r')
            break;

        // make value null terminated
        *p++ = '\0';

        // skip of the LF as well
        if (*p == '\n')
            p++;

        // ignore empty values
        if (*value == '\0')
            continue;

        if (strcasecmp(name, "ST") == 0)
        {
            st = value;
        }
        else if (strcasecmp(name, "MX") == 0)
        {
            mx = value;
            char *mx_end = NULL;
            mx_val = strtol(value, &mx_end, 10);
            if (mx_end == value)
                mx_val = -1;
        }
        else if (strcasecmp(name, "MAN") == 0)
        {
            man = value;
        }
    }

    // check for errors
    if (!man || (strncmp(man, "\"ssdp:discover\"", 15) != 0))
    {
        PRINT_LOG(E_DEBUG,
                  "WARNING: Ignoring invalid SSDP M-SEARCH from %s [bad MAN header '%s']\n",
                  inet_ntoa_ts(sendername.sin_addr), man);
        return;
    }
    else if (!mx || mx_val < 0)
    {
        PRINT_LOG(E_DEBUG,
                  "WARNING: Ignoring invalid SSDP M-SEARCH from %s [bad MX header '%s']\n",
                  inet_ntoa_ts(sendername.sin_addr), mx);
        return;
    }
    else if (!st)
    {
        PRINT_LOG(E_DEBUG, "Invalid SSDP M-SEARCH from %s:%d\n",
                  inet_ntoa_ts(sendername.sin_addr), ntohs(sendername.sin_port));
        return;
    }

#ifdef __linux__
    char host[40] = "127.0.0.1";

    /* find the interface we received the msg from */
    for (struct cmsghdr * cmsg = CMSG_FIRSTHDR(&mh); cmsg; cmsg = CMSG_NXTHDR(&mh, cmsg))
    {
        /* ignore the control headers that don't match what we want */
        if (cmsg->cmsg_level != IPPROTO_IP || cmsg->cmsg_type != IP_PKTINFO)
            continue;

        const struct in_pktinfo *pi = (struct in_pktinfo *)CMSG_DATA(cmsg);
        struct in_addr addr = pi->ipi_spec_dst;
        inet_ntop(AF_INET, &addr, host, sizeof(host));
    }
#else
    /* find in which sub network the client is */
    int iface = get_interface(&sendername.sin_addr);

    if (iface == -1)
    {
        PRINT_LOG(E_DEBUG,
                  "Ignoring SSDP M-SEARCH on other interface [%s]\n",
                  inet_ntoa_ts(sendername.sin_addr));
        return;
    }

    const char *host = get_interface_ip_str(iface);
#endif
    PRINT_LOG(E_DEBUG,
              "SSDP M-SEARCH from %s:%d ST: %s, MX: %s, MAN: %s\n",
              inet_ntoa_ts(sendername.sin_addr), ntohs(sendername.sin_port), st, mx, man);

    /* Responds to request with a device as ST header */
    int st_len = strlen(st);
    for (int i = 0; known_service_types[i]; i++)
    {
        int l = strlen(known_service_types[i]);

        if (st_len < l)
            continue;

        if (memcmp(st, known_service_types[i], l) != 0)
            continue;

        if (st_len > l)
        {
            /* Check version number - we only support 1. */
            if (st[l - 1] == ':' && st[l] == '1')
                l++;
            while (l < st_len)
            {
                if (isdigit(st[l]))
                    break;
                if (isspace(st[l]))
                {
                    l++;
                    continue;
                }
                PRINT_LOG(E_DEBUG,
                          "Ignoring SSDP M-SEARCH with bad extra data '%c' [%s]\n",
                          st[l], inet_ntoa_ts(sendername.sin_addr));
                break;
            }
            if (l != st_len)
                break;
        }
        microsleep(random() >> 20);
        send_ssdp_response(sssdp, sendername, i, host);
        return;
    }
    /* Responds to request with ST: ssdp:all */
    if (strcmp(st, "ssdp:all") == 0)
    {
        for (int i = 0; known_service_types[i]; i++)
        {
            send_ssdp_response(sssdp, sendername, i, host);
        }
    }
}

/* This will broadcast ssdp:byebye notifications to inform
 * the network that UPnP is going down. */
int send_ssdp_goodbyes(int s)
{
    struct sockaddr_in sockname;
    int ret = 0;
    char bufr[300];

    memset(&sockname, 0, sizeof(struct sockaddr_in));
    sockname.sin_family = AF_INET;
    sockname.sin_port = htons(ssdp_port);
    sockname.sin_addr.s_addr = inet_addr(ssdp_mcast_addr);

    for (int dup = 0; dup < 2; dup++)
    {
        for (int i = 0; known_service_types[i]; i++)
        {
            int l = snprintf(bufr, sizeof(bufr),
                             "NOTIFY * HTTP/1.1\r\n"
                             "HOST: %s:%d\r\n"
                             "NT: %s%s\r\n"
                             "USN: %s%s%s%s\r\n"
                             "NTS: ssdp:byebye\r\n"
                             "\r\n",
                             ssdp_mcast_addr, ssdp_port,
                             known_service_types[i],
                             (i > 1 ? "1" : ""), uuidvalue,
                             (i > 0 ? "::" : ""),
                             (i > 0 ? known_service_types[i] : ""), (i > 1 ? "1" : ""));
            PRINT_LOG(E_DEBUG, "Sending ssdp:byebye [%d]\n", s);
            int n = sendto(s, bufr, l, 0,
                           (struct sockaddr *)&sockname, sizeof(struct sockaddr_in));
            if (n < 0)
            {
                PRINT_LOG(E_ERROR, "sendto(udp_shutdown=%d): %d\n", s, errno);
                ret = -1;
                break;
            }
        }
    }
    return ret;
}
