/* MicroDLNA project
 * http://microdlna.sourceforge.net/
 *
 * MicroDLNA media server
 * Copyright (c) 2016, Gabor Simon
 * All rights reserved.
 *
 * Based on the MiniDLNA project:
 * Copyright (C) 2008-2009  Justin Maggard
 *
 * This file is part of MicroDLNA.
 *
 * MicroDLNA is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * MicroDLNA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MicroDLNA. If not, see <http://www.gnu.org/licenses/>.
 *
 * Portions of the code from the MiniUPnP project:
 *
 * Copyright (c) 2006-2007, Thomas Bernard
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
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "stream.h"
#include "upnpevents.h"
#include "log.h"
#include "microdlnapath.h"
#include "upnpdescgen.h"
#include "utils.h"
#include "globalvars.h"
#include "upnphttp.h"

enum SubscriberServiceEnum
{
    EContentDirectory = 1,
    EConnectionManager,
    EMSMediaReceiverRegistrar
};

enum EventType
{
    E_INVALID,
    E_SUBSCRIBE,
    E_RENEW
};

/* stuctures definitions */
struct subscriber
{
    struct subscriber *next;
    struct upnp_event_notify *notify;
    time_t timeout;
    uint32_t seq;
    enum SubscriberServiceEnum service;
    char uuid[42];
    char callback[];
};

struct upnp_event_notify
{
    struct upnp_event_notify *next;
    int s;                      /* socket */
    enum
    { ECreated = 1,
      EConnecting,
      ESending,
      EWaitingForResponse,
      EFinished,
      EError } state;
    struct subscriber *sub;
    int tosend;
    int sent;
    const char *path;
    char addrstr[16];
    char portstr[8];
};

/* prototype */
static void upnp_event_create_notify(struct subscriber *sub);

/* subscriber list */
static struct subscriber *subscriberlist = NULL;

/* notify list */
static struct upnp_event_notify *notifylist = NULL;

/* creates a new subscriber and adds it to the subscriber list
 * also initiate 1st notify */
static char *add_upnpevent_subscriber(const char *eventurl, const char *callback,
                                      int timeout)
{
    PRINT_LOG(E_DEBUG, "add_upnpevent_subscriber(%s, %s, %d)\n", eventurl,
              callback, timeout);

    if (!eventurl || !callback)
        return NULL;

    enum SubscriberServiceEnum srv;
    if (strcmp(eventurl, CONTENTDIRECTORY_EVENTURL) == 0)
        srv = EContentDirectory;
    else if (strcmp(eventurl, CONNECTIONMGR_EVENTURL) == 0)
        srv = EConnectionManager;
    else if (strcmp(eventurl, X_MS_MEDIARECEIVERREGISTRAR_EVENTURL) == 0)
        srv = EMSMediaReceiverRegistrar;
    else
        return NULL;

    struct subscriber *ns = safe_malloc(sizeof(struct subscriber) + strlen(callback) + 1);
    memset(ns, 0, sizeof(struct subscriber) + 1);

    ns->service = srv;
    strcpy(ns->callback, callback);

    if (timeout)
        ns->timeout = time(NULL) + timeout;

    /* make a dummy uuid */
    strxcpy(ns->uuid, uuidvalue, sizeof(ns->uuid));
    ns->uuid[sizeof(ns->uuid) - 1] = '\0';
    snprintf(ns->uuid + 37, 5, "%04lx", random() & 0xffff);

    ns->next = subscriberlist;
    subscriberlist = ns;

    upnp_event_create_notify(ns);
    return ns->uuid;
}

/* renew a subscription (update the timeout) */
static int renew_upnpevent_subscriber(const char *sid, int timeout)
{
    for (struct subscriber * sub = subscriberlist; sub != NULL; sub = sub->next)
    {
        if (memcmp(sid, sub->uuid, 41) == 0)
        {
            sub->timeout = (timeout ? time(NULL) + timeout : 0);
            return 0;
        }
    }
    return -1;
}

static int remove_upnpevent_subscriber(const char *sid)
{
    if (!sid)
        return -1;

    PRINT_LOG(E_DEBUG, "removeSubscriber(%s)\n", sid);

    struct subscriber *sub = subscriberlist;
    struct subscriber **container = &subscriberlist;

    while (sub != NULL)
    {
        if (memcmp(sid, sub->uuid, 41) == 0)
        {
            if (sub->notify)
            {
                sub->notify->sub = NULL;
            }

            *container = sub->next;
            PRINT_LOG(E_DEBUG, "removing subscriber %s\n", sub->uuid);
            free(sub);
            return 0;
        }

        container = &(sub->next);
        sub = *container;
    }
    return -1;
}

void clear_upnpevent_subscribers(void)
{
    struct subscriber *sub = subscriberlist;
    struct subscriber *next;

    while (sub != NULL)
    {
        next = sub->next;
        free(sub);
        sub = next;
    }
    subscriberlist = NULL;
}

/* create and add the notify object to the list */
static void upnp_event_create_notify(struct subscriber *sub)
{
    struct upnp_event_notify *obj;
    int flags;

    obj = safe_malloc(sizeof(struct upnp_event_notify));

    obj->sub = sub;
    obj->state = ECreated;
    obj->s = socket(PF_INET, SOCK_STREAM, 0);
    if (obj->s < 0)
    {
        PRINT_LOG(E_ERROR, "upnp_event_create_notify: socket(): %d\n", errno);
        goto error;
    }
    if ((flags = fcntl(obj->s, F_GETFL, 0)) < 0)
    {
        PRINT_LOG(E_ERROR, "upnp_event_create_notify: fcntl(..F_GETFL..): %d\n", errno);
        goto error;
    }
    if (fcntl(obj->s, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        PRINT_LOG(E_ERROR, "upnp_event_create_notify: fcntl(..F_SETFL..): %d\n", errno);
        goto error;
    }
    if (sub)
        sub->notify = obj;

    obj->next = notifylist;
    notifylist = obj;

    return;
error:
    if (obj->s >= 0)
        close(obj->s);
    free(obj);
}

static void upnp_event_notify_connect(struct upnp_event_notify *obj)
{
    if (!obj)
        return;
    if (obj->sub == NULL)
    {
        obj->state = EError;
        return;
    }

    unsigned int i = 0;
    const char *p = obj->sub->callback;
    p += 7;                     /* http:// */
    while (*p != '/' && *p != ':' && i < (sizeof(obj->addrstr) - 1))
        obj->addrstr[i++] = *(p++);
    obj->addrstr[i] = '\0';

    unsigned short lport;
    if (*p == ':')
    {
        obj->portstr[0] = *p;
        i = 1;
        p++;
        lport = (unsigned short)atoi(p);
        while (*p != '/' && *p != '\0')
        {
            if (i < 7)
                obj->portstr[i++] = *p;
            p++;
        }
        obj->portstr[i] = 0;
    }
    else
    {
        lport = 80;
        obj->portstr[0] = '\0';
    }
    obj->path = *p ? p : "/";

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    inet_aton(obj->addrstr, &addr.sin_addr);
    addr.sin_port = htons(lport);
    PRINT_LOG(E_DEBUG, "%s: '%s' %hu '%s'\n", "upnp_event_notify_connect",
              obj->addrstr, lport, obj->path);
    obj->state = EConnecting;
    if (connect(obj->s, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        if (errno != EINPROGRESS && errno != EWOULDBLOCK)
        {
            PRINT_LOG(E_ERROR, "upnp_event_notify_connect: connect(): %d\n", errno);
            obj->state = EError;
        }
    }
}

static void upnp_event_prepare(struct upnp_event_notify *obj)
{
    if (obj->sub == NULL || obj->s == -1)
    {
        obj->state = EError;
        return;
    }

    int nfd = dup(obj->s);
    if (nfd == -1)
    {
        perror("dup failed");
        return;
    }

    struct stream *fh = sdopen(nfd);
    if (!fh)
    {
        PRINT_LOG(E_DEBUG, "Failed to reopen filehandle\n");
        return;
    }

    PRINT_LOG(E_DEBUG, "Sending UPnP Event response\n");

    stream_printf(fh, "NOTIFY %s HTTP/1.1\r\n"
                  "Host: %s%s\r\n"
                  "Content-Type: text/xml; charset=\"utf-8\"\r\n"
                  "Transfer-Encoding: chunked\r\n"
                  "NT: upnp:event\r\n"
                  "NTS: upnp:propchange\r\n"
                  "SID: %s\r\n"
                  "SEQ: %u\r\n"
                  "Connection: close\r\n"
                  "Cache-Control: no-cache\r\n"
                  "\r\n", obj->path, obj->addrstr, obj->portstr, obj->sub->uuid,
                  obj->sub->seq);

    switch (obj->sub->service)
    {
    case EContentDirectory:
        get_vars_content_directory(fh);
        break;

    case EConnectionManager:
        get_vars_connection_manager(fh);
        break;

    default:
        break;
    }

    int r = chunk_print_end(fh);
    sdclose(fh);

    if (r == EOF)
        obj->state = EError;
    else
        obj->state = EWaitingForResponse;
}

static void upnp_event_recv(struct upnp_event_notify *obj)
{
    // receive a message but ignore it!
    int n = recv(obj->s, NULL, 0, MSG_TRUNC);

    if (n < 0)
    {
        PRINT_LOG(E_ERROR, "upnp_event_recv: recv(): %d\n", errno);
        obj->state = EError;
        return;
    }
    PRINT_LOG(E_DEBUG, "upnp_event_recv: (%dbytes)\n", n);
    obj->state = EFinished;
    if (obj->sub)
    {
        obj->sub->seq++;
        if (!obj->sub->seq)
            obj->sub->seq++;
    }
}

static void upnp_event_process_notify(struct upnp_event_notify *obj)
{
    switch (obj->state)
    {
    case EConnecting:
        /* now connected or failed to connect */
        upnp_event_prepare(obj);
        break;

    case EWaitingForResponse:
        upnp_event_recv(obj);
        break;

    case EFinished:
        close(obj->s);
        obj->s = -1;
        break;

    default:
        PRINT_LOG(E_ERROR, "upnp_event_process_notify: unknown state\n");
    }
}

void upnpevents_selectfds(fd_set *readset, fd_set *writeset, int *max_fd)
{
    struct upnp_event_notify *obj;

    for (obj = notifylist; obj != NULL; obj = obj->next)
    {
        PRINT_LOG(E_DEBUG, "upnpevents_selectfds: %p %d %d\n", obj, obj->state, obj->s);
        if (obj->s >= 0)
        {
            switch (obj->state)
            {
            case ECreated:
                upnp_event_notify_connect(obj);
                if (obj->state != EConnecting)
                    break;

            case EConnecting:
            case ESending:
                FD_SET(obj->s, writeset);
                if (obj->s > *max_fd)
                    *max_fd = obj->s;
                break;

            case EWaitingForResponse:
                FD_SET(obj->s, readset);
                if (obj->s > *max_fd)
                    *max_fd = obj->s;
                break;

            default:
                break;
            }
        }
    }
}

void upnpevents_processfds(fd_set *readset, fd_set *writeset)
{
    struct upnp_event_notify *obj;

    for (obj = notifylist; obj != NULL; obj = obj->next)
    {
        PRINT_LOG(E_DEBUG, "upnpevents_processfds: %p %d %d %d %d\n",
                  obj, obj->state, obj->s,
                  FD_ISSET(obj->s, readset), FD_ISSET(obj->s, writeset));

        if (obj->s >= 0)
        {
            if (FD_ISSET(obj->s, readset) || FD_ISSET(obj->s, writeset))
                upnp_event_process_notify(obj);
        }
    }

    for (struct upnp_event_notify ** cur = &notifylist; *cur != NULL;)
    {
        obj = *cur;

        if (obj->state == EError || obj->state == EFinished)
        {
            if (obj->s >= 0)
            {
                close(obj->s);
            }
            if (obj->sub)
                obj->sub->notify = NULL;

            *cur = obj->next;
            free(obj);
        }
        else
        {
            cur = &(obj->next);
        }
    }
}

void upnpevents_removed_timedout_subs(void)
{
    /* remove timeouted subscribers */
    time_t curtime = time(NULL);
    struct subscriber *sub;
    struct subscriber **cur = &subscriberlist;

    while (*cur != NULL)
    {
        sub = *cur;

        if (sub->timeout && curtime > sub->timeout && sub->notify == NULL)
        {
            *cur = sub->next;
            free(sub);
        }
        else
        {
            cur = &(sub->next);
        }
    }
}

void upnpevents_clear_notify_list(void)
{
    struct upnp_event_notify *obj = notifylist;

    while (obj != NULL)
    {
        struct upnp_event_notify *next = obj->next;

        if (obj->s >= 0)
            close(obj->s);

        free(obj);
        obj = next;
    }

    notifylist = NULL;
}

static int check_event(struct upnphttp *h)
{
    enum EventType type;

    if (h->req_callback)
    {
        if (h->req_sid || !h->req_nt)
        {
            send_http_response(h, HTTP_BAD_REQUEST_400);
            type = E_INVALID;
        }
        else if (strncmp(h->req_callback, "http://", 7) != 0 ||
                 strcmp(h->req_nt, "upnp:event") != 0)
        {
            /* Missing or invalid CALLBACK : 412 Precondition Failed.
             * If CALLBACK header is missing or does not contain a valid HTTP URL,
             * the publisher must respond with HTTP error 412 Precondition Failed*/
            send_http_response(h, HTTP_PRECONDITION_FAILED_412);
            type = E_INVALID;
        }
        else
            type = E_SUBSCRIBE;
    }
    else if (h->req_sid)
    {
        /* subscription renew */
        if (h->req_nt)
        {
            send_http_response(h, HTTP_BAD_REQUEST_400);
            type = E_INVALID;
        }
        else
            type = E_RENEW;
    }
    else
    {
        send_http_response(h, HTTP_PRECONDITION_FAILED_412);
        type = E_INVALID;
    }

    return type;
}

void process_http_subscribe_upnphttp(struct upnphttp *h)
{
    char *sid;

    PRINT_LOG(E_DEBUG, "ProcessHTTPSubscribe %s\n", h->path);
    PRINT_LOG(E_DEBUG, "Callback '%s' Timeout=%d\n", h->req_callback, h->req_timeout);
    PRINT_LOG(E_DEBUG, "SID '%s'\n", h->req_sid);

    switch (check_event(h))
    {
    case E_SUBSCRIBE:
        /* - add to the subscriber list
         * - respond HTTP/x.x 200 OK
         * - Send the initial event message */
        /* Server:, SID:; Timeout: Second-(xx|infinite) */
        sid = add_upnpevent_subscriber(h->path, h->req_callback, h->req_timeout);
        h->respflags = FLAG_TIMEOUT;
        if (sid)
        {
            PRINT_LOG(E_DEBUG, "generated sid=%s\n", sid);
            h->respflags |= FLAG_SID;
            free(h->req_sid);
            h->req_sid = safe_strdup(sid);
        }
        send_http_response(h, HTTP_OK_200);
        break;

    case E_RENEW:
        /* subscription renew */
        if (renew_upnpevent_subscriber(h->req_sid, h->req_timeout) < 0)
        {
            /* Invalid SID
               412 Precondition Failed. If a SID does not correspond to a known,
               un-expired subscription, the publisher must respond
               with HTTP error 412 Precondition Failed. */
            send_http_response(h, HTTP_PRECONDITION_FAILED_412);
        }
        else
        {
            /* A DLNA device must enforce a 5 minute timeout */
            h->respflags = FLAG_TIMEOUT;
            h->req_timeout = 300;
            h->respflags |= FLAG_SID;
            send_http_response(h, HTTP_OK_200);
        }
        break;

    default:
        break;
    }
}

void process_http_un_subscribe_upnphttp(struct upnphttp *h)
{
    PRINT_LOG(E_DEBUG, "ProcessHTTPUnSubscribe %s\n", h->path);
    PRINT_LOG(E_DEBUG, "SID '%s'\n", h->req_sid);

    /* Remove from the list */
    if (check_event(h) != E_INVALID)
    {
        if (remove_upnpevent_subscriber(h->req_sid) < 0)
            send_http_response(h, HTTP_PRECONDITION_FAILED_412);
        else
            send_http_response(h, HTTP_OK_200);
    }
}
