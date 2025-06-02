/* MiniUPnP project
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
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(sun)
#include <sys/sockio.h>
#endif

#include <ifaddrs.h>
#ifdef __linux__
# ifndef AF_LINK
#  define AF_LINK AF_INET
# endif
#else
# include <net/if_dl.h>
#endif
#ifndef IFF_SLAVE
# define IFF_SLAVE 0
#endif

#include "globalvars.h"
#include "getifaddr.h"
#include "log.h"
#include "minissdp.h"
#include "utils.h"

#define MACADDR_IS_ZERO(x) \
        ((x[0] == 0x00) && \
         (x[1] == 0x00) && \
         (x[2] == 0x00) && \
         (x[3] == 0x00) && \
         (x[4] == 0x00) && \
         (x[5] == 0x00))

static int n_lan_addr = 0;

/* list of configured network interfaces */
#define MAX_LAN_ADDR 4

static char *ifaces[MAX_LAN_ADDR] = { NULL, NULL, NULL, NULL };

static struct lan_addr_s lan_addr[MAX_LAN_ADDR];

static void getifaddr(const char *ifname, int sssdp, const struct ifaddrs *ifap)
{
    struct lan_addr_s *cur_lan_addr = &lan_addr[n_lan_addr];
    const struct ifaddrs *p;

    for (p = ifap; p != NULL; p = p->ifa_next)
    {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET)
            continue;
        if (ifname && strcmp(p->ifa_name, ifname) != 0)
            continue;
        if (!ifname && (p->ifa_flags & (IFF_LOOPBACK | IFF_SLAVE)))
            continue;

        const struct in_addr *ipaddress = &((struct sockaddr_in *)p->ifa_addr)->sin_addr;
        memcpy(&cur_lan_addr->addr, ipaddress, sizeof(cur_lan_addr->addr));
        if (!inet_ntop(AF_INET, ipaddress, cur_lan_addr->str, sizeof(lan_addr[0].str)))
        {
            PRINT_LOG(E_ERROR, "inet_ntop(): %d\n", errno);
            continue;
        }

        const struct in_addr *netmask = &((struct sockaddr_in *)p->ifa_netmask)->sin_addr;
        memcpy(&cur_lan_addr->mask, netmask, sizeof(cur_lan_addr->mask));

        cur_lan_addr->ifindex = if_nametoindex(p->ifa_name);
        cur_lan_addr->snotify = open_ssdp_notify_socket(cur_lan_addr, sssdp);

        if (cur_lan_addr->snotify >= 0)
            cur_lan_addr = &lan_addr[++n_lan_addr];

        if (ifname || n_lan_addr >= MAX_LAN_ADDR)
            break;
    }
}

static int getsyshwaddr(char *buf, int len)
{
    unsigned char mac[6];
    int ret = -1;

#if !defined (__linux__) && !defined (__sun__)
    struct ifaddrs *ifap, *p;
    struct sockaddr_in *addr_in;
    uint8_t a;

    if (getifaddrs(&ifap) != 0)
    {
        PRINT_LOG(E_ERROR, "getifaddrs(): %d\n", errno);
        return -1;
    }
    for (p = ifap; p != NULL; p = p->ifa_next)
    {
        if (p->ifa_addr && p->ifa_addr->sa_family == AF_LINK)
        {
            addr_in = (struct sockaddr_in *)p->ifa_addr;
            a = (htonl(addr_in->sin_addr.s_addr) >> 0x18) & 0xFF;
            if (a == 127)
                continue;

            struct sockaddr_dl *sdl;
            sdl = (struct sockaddr_dl *)p->ifa_addr;
            memcpy(mac, LLADDR(sdl), sdl->sdl_alen);
            if (MACADDR_IS_ZERO(mac))
                continue;
            ret = 0;
            break;
        }
    }
    freeifaddrs(ifap);
#else
    struct if_nameindex *net_ifaces, *if_idx;
    struct ifreq ifr;
    int fd;

    memset(&mac, '\0', sizeof(mac));
    /* Get the spatially unique node identifier */
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return ret;

    net_ifaces = if_nameindex();
    if (!net_ifaces)
    {
        close(fd);
        return ret;
    }

    for (if_idx = net_ifaces; if_idx->if_index; if_idx++)
    {
        strxcpy(ifr.ifr_name, if_idx->if_name, IFNAMSIZ);
        if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0)
            continue;
        if (ifr.ifr_ifru.ifru_flags & IFF_LOOPBACK)
            continue;
        if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0)
            continue;
#ifdef __sun__
        if (MACADDR_IS_ZERO(ifr.ifr_addr.sa_data))
            continue;
        memcpy(mac, ifr.ifr_addr.sa_data, 6);
#else
        if (MACADDR_IS_ZERO(ifr.ifr_hwaddr.sa_data))
            continue;
        memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
#endif
        ret = 0;
        break;
    }
    if_freenameindex(net_ifaces);
    close(fd);
#endif
    if (ret == 0)
    {
        if (len > 12)
            sprintf(buf, "%02x%02x%02x%02x%02x%02x",
                    mac[0] & 0xFF, mac[1] & 0xFF, mac[2] & 0xFF,
                    mac[3] & 0xFF, mac[4] & 0xFF, mac[5] & 0xFF);
        else if (len == 6)
            memmove(buf, mac, 6);
    }
    return ret;
}

int validate_uuid(const char *s)
{
    const int lens[] = { 8, 4, 4, 4, 12 };

    for (int i = 0; i < 5; i++)
    {
        if (i > 0 && *s++ != '-')
            return 0;

        for (int j = 0; j < lens[i]; j++, s++)
        {
            if (*s != '\0' && (*s < 'A' || *s > 'F') && (*s < 'a' || *s > 'f')
                && (*s < '0' || *s > '9'))
                return 0;
        }
    }
    return *s == '\0';
}

/* set up uuid based on mac address */
void set_uuid_value(void)
{
    if (uuidvalue[0] != '\0')
        return;

    char mac_str[13];

    // "uuid:00000000-0000-0000-0000-000000000000";
    memcpy(uuidvalue, "uuid:4d696e69-444c-164e-9d41-", 29);
    if (getsyshwaddr(mac_str, sizeof(mac_str)) >= 0)
    {
        strncpy(uuidvalue + 29, mac_str, 13);
    }
    else
    {
        PRINT_LOG(E_ERROR, "No MAC address found. Falling back to generic UUID.\n");
        memcpy(uuidvalue + 29, "554e4b4e4f57", 13);
    }
}

void reload_ifaces(int reload, int sssdp)
{
    int wait = 15;
    for (;;)
    {
        n_lan_addr = 0;
        struct ifaddrs *ifap;
        if (getifaddrs(&ifap) != 0)
            EXIT_ERROR("getifaddrs(): %d\n", errno);

        int i = 0;
        do
        {
            getifaddr(ifaces[i], sssdp, ifap);
            i++;
        } while (i < MAX_LAN_ADDR && ifaces[i]);

        freeifaddrs(ifap);

        for (i = 0; i < n_lan_addr; i++)
        {
            PRINT_LOG(E_INFO, "Enabling interface %s/%s\n",
                      lan_addr[i].str, inet_ntoa_ts(lan_addr[i].mask));

            if (reload)
                send_ssdp_goodbyes(lan_addr[i].snotify);

            send_ssdp_notifies(lan_addr[i].snotify, lan_addr[i].str);
        }

        if (n_lan_addr > 0) return;

        if (reload)
        {
            PRINT_LOG(E_INFO, "Failed to find any network interfaces on reload\n");
            return;
        }

        // retry ...
        PRINT_LOG(E_INFO,
                  "Failed to find any network interfaces (retrying in %d seconds)\n",
                  wait);

        sleep(wait);
        if (wait < 60) wait *= 2;
    }
}


void send_all_ssdp_notifies(void)
{
    for (int i = 0; i < n_lan_addr; i++)
        send_ssdp_notifies(lan_addr[i].snotify, lan_addr[i].str);
}

void send_all_ssdp_goodbyes(void)
{
    for (int i = 0; i < n_lan_addr; i++)
    {
        send_ssdp_goodbyes(lan_addr[i].snotify);
        close(lan_addr[i].snotify);
    }
}

int get_interface(const struct in_addr *client)
{
    for (int i = 0; i < n_lan_addr; i++)
    {
        if ((client->s_addr & lan_addr[i].mask.s_addr)
            == (lan_addr[i].addr.s_addr & lan_addr[i].mask.s_addr))
        {
            return i;
        }
    }
    return -1;
}

const char *get_interface_ip_str(int iface)
{
    if (iface == -1)
        return "127.0.0.1";
    else
        return lan_addr[iface].str;
}

void free_ifaces(void)
{
    free(ifaces[0]);
    for (int i = 0; i < MAX_LAN_ADDR; i++)
        ifaces[i] = NULL;
}

void set_interfaces_from_string(const char *input)
{
    free_ifaces();

    while (isspace(*input))
        input++;

    char *p = safe_strdup(input);
    ifaces[0] = p;

    int num_ifaces = 1;
    for (;;)
    {
        strsep(&p, ",");
        if (p == NULL || *p == '\0')
            break;

        while (isspace(*p))
            p++;

        if (num_ifaces >= MAX_LAN_ADDR)
        {
            PRINT_LOG(E_ERROR, "Too many interfaces (max: %d), ignoring %s\n",
                      MAX_LAN_ADDR, p);
            break;
        }

        ifaces[num_ifaces++] = p;
    }
}
