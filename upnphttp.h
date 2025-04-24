#pragma once
/* MicroDLNA project
 *
 * http://sourceforge.net/projects/microdlna/
 *
 * MicroDLNA media server
 * Copyright (c) 2016, Gabor Simon
 * All rights reserved.
 *
 * Based on the MiniDLNA project:
 * Copyright (C) 2008-2012  Justin Maggard
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

#include <stdint.h>

struct stream;

/* server: HTTP header returned in all HTTP responses : */
#define MICRODLNA_VERSION "0.2"
#define MICRODLNA_SERVER_STRING "MicroDLNA/" MICRODLNA_VERSION

enum HttpCommands
{
    EUnknown = 0,
    EGet,
    EPost,
    EHead,
    ESubscribe,
    EUnSubscribe
};

enum HttpResponseCode
{
    HTTP_OK_200,
    HTTP_BAD_REQUEST_400,
    HTTP_FORBIDDEN_403,
    HTTP_PAGE_NOT_FOUND_404,
    HTTP_NOT_ACCEPTABLE_406,
    HTTP_PRECONDITION_FAILED_412,
    HTTP_INVALID_RANGE_416,
    HTTP_INTERNAL_ERROR_500,
    HTTP_NOT_IMPLEMENTED_501,
    HTTP_SERVICE_UNAVAILABLE_503,
};

struct upnphttp
{
    struct stream *st;
    int fd;
    int iface;

    /* request */
    enum HttpCommands req_command;
    char *path;
    int data_len;
    uint32_t reqflags;

    /* soap action */
    void (*req_soap_action)(struct upnphttp *);
    char *remote_dirpath;
    unsigned int starting_index;
    int requested_count;

    /* For SUBSCRIBE */
    char *req_callback;
    char *req_nt;
    int req_timeout;

    /* For UNSUBSCRIBE */
    char *req_sid;
    off_t req_range_start;
    off_t req_range_end;

    /* response */
    uint32_t respflags;
};

#define FLAG_TIMEOUT            0x00000001
#define FLAG_SID                0x00000002
#define FLAG_RANGE              0x00000004
#define FLAG_HOST               0x00000008
#define FLAG_INVALID_REQ        0x00000040
#define FLAG_HTML               0x00000080

#define FLAG_CHUNKED            0x00000100
#define FLAG_TIMESEEK           0x00000200
#define FLAG_REALTIMEINFO       0x00000400
#define FLAG_PLAYSPEED          0x00000800
#define FLAG_XFERSTREAMING      0x00001000
#define FLAG_XFERINTERACTIVE    0x00002000
#define FLAG_XFERBACKGROUND     0x00004000
#define FLAG_CAPTION            0x00008000

int process_upnphttp_http_query(int s, int iface);

void send_http_headers(struct upnphttp *h, int respcode, const char *respmsg);

void send_http_response(struct upnphttp *h, enum HttpResponseCode code);
