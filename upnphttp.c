/* MicroDLNA project
 *
 * http://sourceforge.net/projects/microdlna/
 *
 * MicroDLNA media server
 * Copyright (c) 2016, Gabor Simon
 * All rights reserved.
 *
 * With alternations by 
 * Michael J.Walsh
 * Copyright (c) 2025
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
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include "stream.h"
#include "getifaddr.h"
#include "globalvars.h"
#include "icons.h"
#include "log.h"
#include "microdlnapath.h"
#include "mime.h"
#include "threads.h"
#include "sendfile.h"
#include "upnpdescgen.h"
#include "upnpevents.h"
#include "upnphttp.h"
#include "upnpsoap.h"
#include "utils.h"
#include "xmlregex.h"
#include "mediadir.h"

#define DLNA_FLAG_DLNA_V1_5      0x00100000 // dlnaVersion15Supported
#define DLNA_FLAG_HTTP_STALLING  0x00200000 // connectionStallingSupported
#define DLNA_FLAG_TM_B           0x00400000 // backgroundTransferModeSupported
#define DLNA_FLAG_TM_I           0x00800000 // interactiveTransferModeSupported
#define DLNA_FLAG_TM_S           0x01000000 // streamingTransferModeSupported

static void send_resp_icon(struct upnphttp *);
static void send_resp_dlnafile(struct upnphttp *);

static struct upnphttp *init_upnphttp_struct(int s, int iface)
{
    if (s < 0)
        return NULL;

    struct upnphttp *ret = (struct upnphttp *)safe_malloc(sizeof(struct upnphttp));

    memset(ret, 0, sizeof(struct upnphttp));

    ret->fd = s;
    ret->iface = iface;
    ret->requested_count = -1;
    ret->st = sdopen(s);
    if (!ret->st)
    {
        free(ret);
        return NULL;
    }

    return ret;
}

static void delete_upnphttp_struct(struct upnphttp *h)
{
    if (!h)
        return;

    if (h->st && sdclose(h->st) < 0)
        PRINT_LOG(E_ERROR, "delete_upnphttp_struct: fclose(%d): %d\n", h->fd, errno);

    free(h->remote_dirpath);
    free(h->req_callback);
    free(h->req_sid);
    free(h->req_nt);
    free(h->path);
    free(h);
}


/* parse HttpHeaders of the REQUEST */
static void parse_http_header(struct upnphttp *h, char *name, char *value, int len)
{
    if (strcasecmp(name, "Content-Length") == 0)
    {
        h->data_len = atoi(value);
        if (h->data_len < 0)
        {
            PRINT_LOG(E_DEBUG, "Invalid Content-Length %d", h->data_len);
            h->data_len = 0;
        }
    }
    else if (strcasecmp(name, "SOAPAction") == 0)
    {
        // urn:schemas-upnp-org:service:ContentDirectory:1#Browse
        char *start = strchr(value, '#');
        if (start)
            value = start + 1;

        int last = strcspn(value, "'\"");
        value[last] = '\0';

        PRINT_LOG(E_DEBUG, "SoapMethod: %s\n", value);

        if (strcmp("Browse", value) == 0)
            h->req_soap_action = browse_content_directory;
        else if (strcmp("Search", value) == 0)
            h->req_soap_action = unsupported_soap_action;
        else if (strcmp("GetSearchCapabilities", value) == 0)
            h->req_soap_action = get_search_capabilities;
        else if (strcmp("GetSortCapabilities", value) == 0)
            h->req_soap_action = get_sort_capabilities;
        else if (strcmp("GetProtocolInfo", value) == 0)
            h->req_soap_action = get_protocol_info;
        else
            h->req_soap_action = invalid_soap_action;
    }
    else if (strcasecmp(name, "Callback") == 0)
    {
        if (len >= 2 && value[0] == '<' && value[len - 1] == '>')
        {
            value[len - 1] = '\0';
            value++;
        }
        free(h->req_callback);
        h->req_callback = safe_strdup(value);
    }
    else if (strcasecmp(name, "SID") == 0)
    {
        free(h->req_sid);
        h->req_sid = safe_strdup(value);
    }
    else if (strncasecmp(name, "NT", 2) == 0)
    {
        free(h->req_nt);
        h->req_nt = safe_strdup(value);
    }
    /* Timeout: Seconds-nnnn */
    /* TIMEOUT
       Recommended. Requested duration until subscription expires,
       either number of seconds or infinite. Recommendation
       by a UPnP Forum working committee. Defined by UPnP vendor.
       Consists of the keyword "Second-" followed (without an
       intervening space) by either an integer or the keyword "infinite". */
    else if (strncasecmp(name, "Timeout", 7) == 0)
    {
        if (strncasecmp(value, "Second-", 7) == 0)
            h->req_timeout = atoi(value + 7);
    }
    // Range: bytes=xxx-yyy
    else if (strncasecmp(name, "Range", 5) == 0)
    {
        if (strncasecmp(value, "bytes=", 6) == 0)
        {
            value += 6;

            h->reqflags |= FLAG_RANGE;
            h->req_range_start = strtoll(value, &value, 10);
            h->req_range_end = *value != '\0' ? atoll(value + 1) : 0;

            PRINT_LOG(E_DEBUG, "Range Start-End: %lld - %lld\n",
                      (long long)h->req_range_start,
                      h->req_range_end ? (long long)h->req_range_end : -1);
        }
    }
    // Be strict on host header to prevent DNS rebinding attacks
    // host should be present and match listening interface
    else if (strcasecmp(name, "Host") == 0)
    {
        char expected_host[30];

        if (listening_port == 80)
            strxcpy(expected_host, get_interface_ip_str(h->iface), 30);
        else
            snprintf(expected_host, 30, "%s:%d", get_interface_ip_str(h->iface),
                     listening_port);

        if (strncmp(expected_host, value, 30) == 0)
            h->reqflags |= FLAG_HOST;
        else
            PRINT_LOG(E_DEBUG, "Host heading mismatch: %s != %s\n", expected_host, value);
    }
    else if (strcasecmp(name, "Transfer-Encoding") == 0)
    {
        if (strcasecmp(value, "chunked") == 0)
            h->reqflags |= FLAG_CHUNKED;
    }
    else if (strcasecmp(name, "TimeSeekRange.dlna.org") == 0)
    {
        h->reqflags |= FLAG_TIMESEEK;
    }
    else if (strcasecmp(name, "PlaySpeed.dlna.org") == 0)
    {
        h->reqflags |= FLAG_PLAYSPEED;
    }
    else if (strcasecmp(name, "realTimeInfo.dlna.org") == 0)
    {
        h->reqflags |= FLAG_REALTIMEINFO;
    }
    else if (strcasecmp(name, "getcontentFeatures.dlna.org") == 0)
    {
        if (strcasecmp(value, "1") != 0)
            h->reqflags |= FLAG_INVALID_REQ;
    }
    else if (strcasecmp(name, "getAvailableSeekRange.dlna.org") == 0)
    {
        if (strcasecmp(value, "1") != 0)
            h->reqflags |= FLAG_INVALID_REQ;
    }
    else if (strcasecmp(name, "transferMode.dlna.org") == 0)
    {
        if (strcasecmp(value, "Streaming") == 0)
            h->reqflags |= FLAG_XFERSTREAMING;
        else if (strcasecmp(value, "Interactive") == 0)
            h->reqflags |= FLAG_XFERINTERACTIVE;
        else if (strcasecmp(value, "Background") == 0)
            h->reqflags |= FLAG_XFERBACKGROUND;
    }
    else if (strcasecmp(name, "getCaptionInfo.sec") == 0)
    {
        h->reqflags |= FLAG_CAPTION;
    }
}

static void send_http_response_helper(struct upnphttp *h, int code, const char *msg)
{
    h->respflags = FLAG_HTML;
    send_http_headers(h, code, msg);
    if (h->req_command != EHead)
    {
        CHUNK_PRINT_ALL(h->st,
                        "<!DOCTYPE html><html><head><title>",
                        msg, "</title></head><body><h1>", msg, "</h1></body></html>");

        chunk_print_end(h->st);
    }
}

void send_http_response(struct upnphttp *h, enum HttpResponseCode code)
{
    switch (code)
    {
    case HTTP_OK_200:
        return send_http_response_helper(h, 200, "OK");

    case HTTP_BAD_REQUEST_400:
        return send_http_response_helper(h, 400, "Bad Request");

    case HTTP_FORBIDDEN_403:
        return send_http_response_helper(h, 403, "Forbidden");

    case HTTP_PAGE_NOT_FOUND_404:
        return send_http_response_helper(h, 404, "Page Not Found");

    case HTTP_NOT_ACCEPTABLE_406:
        return send_http_response_helper(h, 406, "Not Acceptable");

    case HTTP_PRECONDITION_FAILED_412:
        return send_http_response_helper(h, 412, "Precondition Failed");

    case HTTP_INVALID_RANGE_416:
        return send_http_response_helper(h, 416, "Invalid Range Request");

    default:
    case HTTP_INTERNAL_ERROR_500:
        return send_http_response_helper(h, 500, "Internal Server Error");

    case HTTP_NOT_IMPLEMENTED_501:
        return send_http_response_helper(h, 501, "Http Version Not Supported");

    case HTTP_SERVICE_UNAVAILABLE_503:
        return send_http_response_helper(h, 503, "Http Service Unavailable");
    }
}

/* Sends the description generated by the parameter */
static void send_xml_desc(struct upnphttp *h, void(func) (struct stream *))
{
    send_http_headers(h, 200, "OK");

    if (h->req_command != EHead)
    {
        func(h->st);
        chunk_print_end(h->st);
    }
}

static int readline(int fd, char *buf, int limit)
{
    int len = 0;

    while (len < limit && read(fd, buf, 1) > 0)
    {
        if (*buf == '\r')
        {
            *buf = '\0';

            char next;
            if (read(fd, &next, 1) > 0 && next == '\n')
                return len;
            else
                return -1;
        }

        buf++;
        len++;
    }
    return -1;
}

/* Parse and process Http Query
 * called once all the HTTP headers have been received. */
int process_upnphttp_http_query(int s, int iface)
{
    // allocate a struct
    struct upnphttp *h = init_upnphttp_struct(s, iface);

    if (!h)
        goto close;

    // set a 20 second timeout for activity on incoming connections
    struct timeval to = { .tv_sec = 20, .tv_usec = 0 };
    if (setsockopt(h->fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(struct timeval)))
        PRINT_LOG(E_ERROR, "setsockopt(http, SO_RCVTIMEO): %d\n", errno);

    // get the first line from the buf
    char buf[1024];
    int len = readline(h->fd, &buf[0], 1024);
    if (len < 1)
    {
        PRINT_LOG(E_DEBUG, "Received bad http request\n");
        goto close;
    }

    // split path and method
    char *path_tmp = strchr(&buf[0], ' ');
    if (path_tmp == NULL)
    {
        PRINT_LOG(E_DEBUG, "Received bad http request\n");
        goto close;
    }
    *path_tmp = '\0';
    path_tmp++;

    // check method
    if (strcmp(&buf[0], "GET") == 0)
        h->req_command = EGet;
    else if (strcmp(&buf[0], "HEAD") == 0)
        h->req_command = EHead;
    else if (strcmp(&buf[0], "POST") == 0)
        h->req_command = EPost;
    else if (strcmp(&buf[0], "SUBSCRIBE") == 0)
        h->req_command = ESubscribe;
    else if (strcmp(&buf[0], "UNSUBSCRIBE") == 0)
        h->req_command = EUnSubscribe;
    else
    {
        PRINT_LOG(E_DEBUG, "Unsupported HTTP Command %s\n", &buf[0]);
        send_http_response(h, HTTP_NOT_IMPLEMENTED_501);
        goto close;
    }

    // check protocol
    if (strcmp(&buf[len - 9], " HTTP/1.1") != 0)
    {
        send_http_response(h, HTTP_NOT_IMPLEMENTED_501);
        goto close;
    }
    buf[len - 9] = '\0';

    if (strncmp(path_tmp, "http://", 7) == 0)
    {
        path_tmp += 7;
        while (*path_tmp != '/' && *path_tmp != '\0')
            path_tmp++;
    }
    else
    {
        while (*path_tmp == ' ')
            path_tmp++;
    }

    // unescape uri-encoded string  (ie %2F)
    url_unescape(path_tmp);

    h->path = safe_strdup(path_tmp);

    // check headers
    int header_no = 0;
    while (header_no < 20 && (len = readline(h->fd, &buf[0], 1024)) > 0)
    {
        int i = 0;
        int l = len;

        // check there's some name
        if (buf[0] == ' ' || buf[0] == ':')
            continue;

        // trim at the end
        while (buf[l - 1] == ' ')
            buf[--l] = '\0';

        // trim first token
        while (i < l && buf[i] != ':' && buf[i] != ' ')
            i++;

        // skip whitespace
        while (buf[i] == ' ')
            buf[i++] = '\0';

        // check there's a colon
        if (buf[i] != ':')
            continue;
        buf[i] = '\0';

        // skip more whitespace
        i++;
        while (buf[i] == ' ')
            buf[i++] = '\0';
        l -= i;

        parse_http_header(h, &buf[0], &buf[i], l);
        header_no++;
    }
    if (len < 0)
    {
        send_http_response(h, HTTP_BAD_REQUEST_400);
        goto close;
    }

    // read post message
    // legitimate http requests should be fairly small so reject anything too big
    if (h->data_len > 2048)
    {
        send_http_response(h, HTTP_BAD_REQUEST_400);
        goto close;
    }
    else if (h->data_len > 0 || h->reqflags & FLAG_CHUNKED)
    {
        process_post_content(h);
    }

    // Be strict on host header to prevent DNS rebinding attacks
    if (!(h->reqflags & FLAG_HOST))
    {
        PRINT_LOG(E_DEBUG, "Missing or invalid host header, responding ERROR 400.\n");
        send_http_response(h, HTTP_BAD_REQUEST_400);
        goto close;
    }
    else if (h->reqflags & FLAG_INVALID_REQ)
    {
        PRINT_LOG(E_DEBUG, "Invalid request, responding ERROR 400.\n");
        send_http_response(h, HTTP_BAD_REQUEST_400);
        goto close;
    }
    /* 7.3.33.4 */
    else if ((h->reqflags & (FLAG_TIMESEEK | FLAG_PLAYSPEED)) &&
             !(h->reqflags & FLAG_RANGE))
    {
        PRINT_LOG(E_DEBUG, "DLNA %s requested, responding ERROR 406\n",
                  h->reqflags & FLAG_TIMESEEK ? "TimeSeek" : "PlaySpeed");
        send_http_response(h, HTTP_NOT_ACCEPTABLE_406);
        goto close;
    }

    switch (h->req_command)
    {
    case EPost:
        if (h->req_soap_action)
            h->req_soap_action(h);
        else
            invalid_soap_action(h);
        break;

    case EGet:
    case EHead:
        if (strcmp(ROOTDESC_PATH, h->path) == 0)
            send_xml_desc(h, gen_root_desc);
        else if (strcmp(CONTENTDIRECTORY_PATH, h->path) == 0)
            send_xml_desc(h, send_content_directory);
        else if (strcmp(CONNECTIONMGR_PATH, h->path) == 0)
            send_xml_desc(h, send_connection_manager);
        else if (strcmp(X_MS_MEDIARECEIVERREGISTRAR_PATH, h->path) == 0)
            send_xml_desc(h, send_x_ms_media_receiver_registrar);
        else if (strncmp(h->path, "/MediaItems", 11) == 0 && (h->path[11] == '/'
                                                              || h->path[11] == '\0'))
        {
            memmove(h->path, h->path + 11, strlen(h->path) - 10);
            send_resp_dlnafile(h);
            h = NULL;           // thread handles deallocation
        }
        else if (strncmp(h->path, "/icons/", 7) == 0)
        {
            memmove(h->path, h->path + 7, strlen(h->path) - 6);
            send_resp_icon(h);
        }
        else
        {
            PRINT_LOG(E_DEBUG, "%s not found, responding ERROR 404\n", h->path);
            send_http_response(h, HTTP_PAGE_NOT_FOUND_404);
        }
        break;

    case ESubscribe:
        process_http_subscribe_upnphttp(h);
        break;

    case EUnSubscribe:
        process_http_un_subscribe_upnphttp(h);
        break;

    default:
        break;
    }

close:
    delete_upnphttp_struct(h);
    return 1;
}

/* Respond with response code and response message */

void send_http_headers(struct upnphttp *h, int respcode, const char *respmsg)
{
    stream_printf(h->st, "HTTP/1.1 %d %s\r\n"
                  "Content-Type: %s; charset=utf-8\r\n"
                  "Connection: close\r\n"
                  "Transfer-Encoding: chunked\r\n"
                  "Server: " MICRODLNA_SERVER_STRING "\r\n",
                  respcode, respmsg,
                  (h->respflags & FLAG_HTML) ? "text/html" : "text/xml");

    /* Additional headers */
    if (h->respflags & FLAG_TIMEOUT)
        stream_printf(h->st,
                      "Timeout: Second-%d\r\n", h->req_timeout ? h->req_timeout : 300);

    if (h->respflags & FLAG_SID)
        stream_printf(h->st, "SID: %s\r\n", h->req_sid);

    char date[30];
    struct tm buf;
    time_t curtime = time(NULL);
    strftime(date, 30, "%a, %d %b %Y %H:%M:%S GMT", gmtime_r(&curtime, &buf));
    stream_printf(h->st, "Date: %s\r\n", date);
    stream_printf(h->st, "EXT:\r\n");
    stream_printf(h->st, "\r\n");
}


static void start_send_http_headers(struct upnphttp *h, int respcode, const char *tmode,
                                    const struct ext_info *mime)
{
    char date[30];
    struct tm buf;
    time_t now = time(NULL);

    strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", gmtime_r(&now, &buf));

    stream_printf(h->st, "HTTP/1.1 %d OK\r\n"
                  "Connection: close\r\n"
                  "Date: %s\r\n"
                  "Server: " MICRODLNA_SERVER_STRING "\r\n"
                  "EXT:\r\n"
                  "realTimeInfo.dlna.org: DLNA.ORG_TLAG=*\r\n"
                  "transferMode.dlna.org: %s\r\n"
                  "Content-Type: %s/%s\r\n",
                  respcode, date, tmode, mime_type_to_text(mime->type), mime->sub_type);
}

static void send_resp_icon(struct upnphttp *h)
{
    struct ext_info mime = { .type = M_IMAGE };
    const char *data;
    int size;

    if (strcmp(h->path, "sm.png") == 0)
    {
        PRINT_LOG(E_DEBUG, "Sending small PNG icon\n");
        data = (char *)png_sm;
        size = sizeof(png_sm) - 1;
        mime.sub_type = "png";
    }
    else if (strcmp(h->path, "lrg.png") == 0)
    {
        PRINT_LOG(E_DEBUG, "Sending large PNG icon\n");
        data = (char *)png_lrg;
        size = sizeof(png_lrg) - 1;
        mime.sub_type = "png";
    }
    else if (strcmp(h->path, "sm.jpg") == 0)
    {
        PRINT_LOG(E_DEBUG, "Sending small JPEG icon\n");
        data = (char *)jpeg_sm;
        size = sizeof(jpeg_sm) - 1;
        mime.sub_type = "jpeg";
    }
    else if (strcmp(h->path, "lrg.jpg") == 0)
    {
        PRINT_LOG(E_DEBUG, "Sending large JPEG icon\n");
        data = (char *)jpeg_lrg;
        size = sizeof(jpeg_lrg) - 1;
        mime.sub_type = "jpeg";
    }
    else
    {
        PRINT_LOG(E_DEBUG, "Invalid icon request: %s\n", h->path);
        send_http_response(h, HTTP_PAGE_NOT_FOUND_404);
        return;
    }

    start_send_http_headers(h, 200, "Interactive", &mime);
    stream_printf(h->st, "Content-Length: %d\r\n\r\n", size);

    if (h->req_command != EHead)
        stream_write(data, size, h->st);
}

static char *get_srt_path(const char *file_path)
{
    int len = strlen(file_path);
    int end = len > 7 ? len - 7 : 0;

    for (int i = len - 1; i > end; i--)
    {
        if (file_path[i] == '.')
        {
            char *p = safe_malloc(i + 5);
            memcpy(p, file_path, i + 1);
            memcpy(p + i + 1, "srt", 4);
            return p;
        }
        else if (file_path[i] == '/')
            return NULL;
    }
    return NULL;
}

static void *serve_file(void *param)
{
    struct upnphttp *h = (struct upnphttp *)param;
    int sendfh = -1;

    url_unescape(h->path);

    if (!sanitise_path(h->path))
    {
        PRINT_LOG(E_ERROR,
                  "Browsing ContentDirectory failed: addressing out of media_dir\n"
                  "\tObject='%s'\n", h->path);
        send_http_response(h, HTTP_NOT_ACCEPTABLE_406);
        goto error;
    }

    PRINT_LOG(E_DEBUG, "Serving DetailID: %s\n", h->path);

    struct stat st;
    if (stat(h->path, &st) != 0)
    {
        PRINT_LOG(E_ERROR, "Stat failed %s/%s\n", media_dir, h->path);
        send_http_response(h, HTTP_PAGE_NOT_FOUND_404);
        goto error;
    }

    if (!S_ISREG(st.st_mode))
    {
        PRINT_LOG(E_ERROR, "Non-regular file: %s/%s\n", media_dir, h->path);
        send_http_response(h, HTTP_FORBIDDEN_403);
        goto error;
    }

    sendfh = open(h->path, O_RDONLY);
    if (sendfh < 0)
    {
        PRINT_LOG(E_ERROR, "Error opening %s/%s\n", media_dir, h->path);
        send_http_response(h, HTTP_PAGE_NOT_FOUND_404);
        goto error;
    }

    // get size
    off_t size = lseek(sendfh, 0, SEEK_END);
    lseek(sendfh, 0, SEEK_SET);

    // get mime type
    const struct ext_info *mime = get_mime_type(h->path);
    if (!mime)
    {
        PRINT_LOG(E_ERROR, "Cannot determine mime type for '%s'\n", h->path);
        send_http_response(h, HTTP_NOT_ACCEPTABLE_406);
        goto error;
    }

    if (h->reqflags & FLAG_XFERSTREAMING)
    {
        if (mime->type == M_IMAGE)
        {
            PRINT_LOG(E_DEBUG,
                      "Client tried to specify transferMode as Streaming with an image!\n");
            send_http_response(h, HTTP_NOT_ACCEPTABLE_406);
            goto error;
        }
    }
    else if (h->reqflags & FLAG_XFERINTERACTIVE)
    {
        if (h->reqflags & FLAG_REALTIMEINFO)
        {
            PRINT_LOG(E_DEBUG, "Bad realTimeInfo flag with Interactive request!\n");
            send_http_response(h, HTTP_BAD_REQUEST_400);
            goto error;
        }
        if (mime->type == M_IMAGE)
        {
            PRINT_LOG(E_DEBUG,
                      "Client tried to specify transferMode as Interactive without an image!\n");
            send_http_response(h, HTTP_NOT_ACCEPTABLE_406);
            goto error;
        }
    }

    const char *tmode;
    if ((h->reqflags & FLAG_XFERBACKGROUND) && (setpriority(PRIO_PROCESS, 0, 19) == 0))
        tmode = "Background";
    else if (mime->type == M_IMAGE)
        tmode = "Interactive";
    else
        tmode = "Streaming";

    if (h->reqflags & FLAG_RANGE)
    {
        if (h->req_range_end == 0)
        {
            h->req_range_end = size - 1;
        }
        if (h->req_range_start > h->req_range_end || h->req_range_start < 0)
        {
            PRINT_LOG(E_DEBUG, "Specified range was invalid!\n");
            send_http_response(h, HTTP_BAD_REQUEST_400);
            close(sendfh);
            goto error;
        }
        if (h->req_range_end >= size)
        {
            PRINT_LOG(E_DEBUG, "Specified range was outside file boundaries!\n");
            send_http_response(h, HTTP_INVALID_RANGE_416);
            close(sendfh);
            goto error;
        }
    }

    start_send_http_headers(h, (h->reqflags & FLAG_RANGE ? 206 : 200), tmode, mime);

    off_t total;
    if (h->reqflags & FLAG_RANGE)
    {
        total = h->req_range_end - h->req_range_start + 1;
        stream_printf(h->st, "Content-Length: %jd\r\n"
                      "Content-Range: bytes %jd-%jd/%jd\r\n",
                      (intmax_t)total, (intmax_t)h->req_range_start,
                      (intmax_t)h->req_range_end, (intmax_t)size);
    }
    else
    {
        h->req_range_end = size - 1;
        total = size;
        stream_printf(h->st, "Content-Length: %jd\r\n", (intmax_t)total);
    }

    // print subtitle header
    if (h->reqflags & FLAG_CAPTION && mime->type == M_VIDEO)
    {
        char *srt_file_path = get_srt_path(h->path);
        if (srt_file_path)
        {
            int subs = open(srt_file_path, O_RDONLY);
            if (subs > -1)
            {
                close(subs);
                const char *escaped_rel_path = url_escape(srt_file_path);

                stream_printf(h->st,
                              "CaptionInfo.sec: http://%s:%d/MediaItems/%s\r\n",
                              get_interface_ip_str(h->iface), listening_port,
                              escaped_rel_path);

                if (escaped_rel_path != srt_file_path)
                    free((void *)escaped_rel_path);
            }

            free(srt_file_path);
        }
    }

    uint32_t dlna_flags = DLNA_FLAG_DLNA_V1_5 | DLNA_FLAG_HTTP_STALLING | DLNA_FLAG_TM_B;
    if (mime->type == M_IMAGE)
        dlna_flags |= DLNA_FLAG_TM_I;
    else
        dlna_flags |= DLNA_FLAG_TM_S;

    stream_printf(h->st, "Accept-Ranges: bytes\r\n"
                  "contentFeatures.dlna.org: DLNA.ORG_OP=01;"
                  "DLNA.ORG_CI=0;DLNA.ORG_FLAGS=%08X"
                  "000000000000000000000000\r\n\r\n", dlna_flags);

    if (h->req_command != EHead)
    {
        stream_flush(h->st);

        // get what we need to run the file transfer
        FILE *fh = h->st->fh;
        int fd = h->fd;
        int start = h->req_range_start;
        int end = h->req_range_end;

        // deallocate everything else, taking care not to close the file handle
        free(h->st);
        h->st = NULL;
        delete_upnphttp_struct(h);
        h = NULL;

        // run the file transfer
        send_file(fd, sendfh, start, end);
        fclose(fh);
    }

error:
    decrement_thread_count();
    delete_upnphttp_struct(h);
    if (sendfh > -1)
        close(sendfh);

    return NULL;
}

static void send_resp_dlnafile(struct upnphttp *h)
{
    // cd to media dir
    if (chdir_to_media_dir() != 0)
    {
        PRINT_LOG(E_ERROR, "Failed to open media_dir\n");
        send_http_response(h, HTTP_SERVICE_UNAVAILABLE_503);
        delete_upnphttp_struct(h);
        return;
    }

    // if thread fails, issue error and perform cleanup
    if (create_thread(serve_file, h) != 0)
    {
        send_http_response(h, HTTP_INTERNAL_ERROR_500);
        delete_upnphttp_struct(h);
    }
}
