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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dirlist.h"
#include "stream.h"
#include "mime.h"
#include "getifaddr.h"
#include "log.h"
#include "upnpdescgen.h"
#include "upnphttp.h"
#include "globalvars.h"
#include "upnpsoap.h"
#include "utils.h"

/* Standard Errors:
 *
 * errorCode errorDescription Description
 * --------    ---------------- -----------
 * 401         Invalid Action     No action by that name at this service.
 * 402         Invalid Args     Could be any of the following: not enough in args,
 *                             too many in args, no in arg by that name,
 *                             one or more in args are of the wrong data type.
 * 403         Out of Sync     Out of synchronization.
 * 501         Action Failed     May be returned in current state of service
 *                             prevents invoking that action.
 * 600-699     TBD             Common action errors. Defined by UPnP Forum
 *                             Technical Committee.
 * 700-799     TBD             Action-specific errors for standard actions.
 *                             Defined by UPnP Forum working committee.
 * 800-899     TBD             Action-specific errors for non-standard actions.
 *                             Defined by UPnP vendor.
 */

#define CONTENT_DIRECTORY_SCHEMAS \
        " xmlns:dc=\"http://purl.org/dc/elements/1.1/\"" \
        " xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\"" \
        " xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\""

static char *beforebody =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
    "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>";

static const char *afterbody = "</s:Body></s:Envelope>\n";

static void copy(char **dest, const char *src)
{
    while (*src != '\0')
    {
        **dest = *src;
        src++;
        (*dest)++;
    }
}

// convert '&' to '&amp;amp;'
static const char *xml_escape_double(const char *input_string)
{
    int normal_chars = 0;
    int escape_chars = 0;

    // before malloc'ing, calculate how much space we need
    const char *r = input_string;

    for (;;)
    {
        switch (*r++)
        {
        case '&':
            escape_chars += 9;
            break;

        case '<':
            escape_chars += 8;
            break;

        case '>':
            escape_chars += 8;
            break;

        case '"':
            escape_chars += 10;
            break;

        case '\'':
            escape_chars += 10;
            break;

        default:
            normal_chars++;
            break;

        case '\0':
            goto end_first_loop;
        }
    }
end_first_loop:

    // we don't need to escape any chars so just return the pointer
    if (escape_chars == 0)
        return input_string;

    char *escaped_string = safe_malloc(normal_chars + escape_chars + 1);
    r = input_string;
    char *w = escaped_string;
    while (1)
    {
        switch (*r)
        {
        case '&':
            copy(&w, "&amp;amp;");
            break;

        case '<':
            copy(&w, "&amp;lt;");
            break;

        case '>':
            copy(&w, "&amp;gt;");
            break;

        case '"':
            copy(&w, "&amp;quot;");
            break;

        case '\'':
            copy(&w, "&amp;apos;");
            break;

        default:
            *w++ = *r;
            break;

        case '\0':
            goto end_second_loop;
        }
        r++;
    }
end_second_loop:
    *w = '\0';
    return escaped_string;
}

static void xml_unescape(char *tag)
{
    char *r = tag;
    char *w = tag;

    while (*r != '\0')
    {
        if (*r == '&')
        {
            if (strncmp(r + 1, "amp;", 4) == 0)
            {
                *w++ = '&';
                r += 5;
            }
            else if (strncmp(r + 1, "lt;", 3) == 0)
            {
                *w++ = '<';
                r += 4;
            }
            else if (strncmp(r + 1, "gt;", 3) == 0)
            {
                *w++ = '>';
                r += 4;
            }
            else if (strncmp(r + 1, "quot;", 5) == 0)
            {
                *w++ = '"';
                r += 6;
            }
            else if (strncmp(r + 1, "apos;", 5) == 0)
            {
                *w++ = '\'';
                r += 6;
            }
            else
            {
                *w++ = '&';
                r += 1;
            }
        }
        else
        {
            *w++ = *r++;
        }
    }
    *w = '\0';
}


static void soap_error(struct upnphttp *h, int err_code, const char *err_desc)
{
    PRINT_LOG(E_DEBUG, "Returning UPnPError %d: %s\n", err_code, err_desc);

    send_http_headers(h, err_code, err_desc);

    if (h->req_command != EHead)
    {
        chunk_printf(h->st,
                     "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                     "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                     "<s:Body>"
                     "<s:Fault>"
                     "<faultcode>s:Client</faultcode>"
                     "<faultstring>UPnPError</faultstring>"
                     "<detail>"
                     "<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\">"
                     "<errorCode>%d</errorCode>"
                     "<errorDescription>%s</errorDescription>"
                     "</UPnPError>"
                     "</detail>"
                     "</s:Fault>"
                     "</s:Body>"
                     "</s:Envelope>", err_code, err_desc);

        chunk_print_end(h->st);
    }
}

void get_protocol_info(struct upnphttp *h)
{
    send_http_headers(h, 200, "OK");

    CHUNK_PRINT_ALL(h->st,
                    beforebody,
                    "<u:GetProtocolInfoResponse "
                    "xmlns:u=\"urn:schemas-upnp-org:service:ConnectionManager:1\">"
                    "<Source>");

    get_resource_protocol_info_values(h->st);

    CHUNK_PRINT_ALL(h->st,
                    "</Source><Sink></Sink></u:GetProtocolInfoResponse>", afterbody);

    chunk_print_end(h->st);
}

void get_sort_capabilities(struct upnphttp *h)
{
    send_http_headers(h, 200, "OK");

    CHUNK_PRINT_ALL(h->st,
                    beforebody,
                    "<u:GetSortCapabilitiesResponse "
                    "xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
                    "<SortCaps>dc:title,</SortCaps>"
                    "</u:GetSortCapabilitiesResponse>", afterbody);

    chunk_print_end(h->st);
}

void get_search_capabilities(struct upnphttp *h)
{
    send_http_headers(h, 200, "OK");

    CHUNK_PRINT_ALL(h->st,
                    beforebody,
                    "<u:GetSearchCapabilitiesResponse "
                    "xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
                    "<SearchCaps>@id, @parentID, @refID </SearchCaps>"
                    "</u:GetSearchCapabilitiesResponse>", afterbody);

    chunk_print_end(h->st);
}

static void print_xml_directory_listing(struct upnphttp *h,
                                        content_entry **entries, int number_returned,
                                        int total_matches)
{
    send_http_headers(h, 200, "OK");

    CHUNK_PRINT_ALL(h->st,
                    beforebody,
                    "<u:BrowseResponse "
                    "xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
                    "<Result>&lt;DIDL-Lite" CONTENT_DIRECTORY_SCHEMAS "&gt;\n");

    char listening_port_str[7];
    if (listening_port == 80)
        listening_port_str[0] = '\0';
    else
        snprintf(listening_port_str, 6, ":%d", listening_port);

    const char *url_escaped_dirpath = url_escape(h->remote_dirpath);
    const char *xml_escaped_dirpath = xml_escape_double(h->remote_dirpath);

    PRINT_LOG(E_DEBUG, "Browsing ContentDirectory: %s\n", h->remote_dirpath);

    for (int i = 0; i < number_returned; i++)
    {
        const char *xml_escaped_filename = xml_escape_double(entries[i]->name);

        if (entries[i]->type == T_DIR)
        {
            CHUNK_PRINT_ALL(h->st,
                            "&lt;container id=\"",
                            xml_escaped_dirpath,
                            "/",
                            xml_escaped_filename,
                            "\" parentID=\"",
                            xml_escaped_dirpath,
                            "\" restricted=\"1\" searchable=\"0\"&gt;&lt;dc:title&gt;",
                            xml_escaped_filename,
                            "&lt;/dc:title&gt;&lt;upnp:class"
                            "&gt;object.container.storageFolder"
                            "&lt;/upnp:class&gt;&lt;upnp:storageUsed"
                            "&gt; -1 &lt;/upnp:storageUsed&gt;"
                            "&lt;/container&gt;");
        }
        else if (entries[i]->type == T_FILE)
        {
            const char *url_escaped_filename = url_escape(entries[i]->name);

            CHUNK_PRINT_ALL(h->st,
                            "&lt;item id=\"",
                            xml_escaped_dirpath,
                            "/",
                            xml_escaped_filename,
                            "\" parentID=\"",
                            xml_escaped_dirpath,
                            "\" restricted=\"1\"&gt;&lt;dc:title&gt;",
                            xml_escaped_filename,
                            "&lt;/dc:title&gt;&lt;upnp:class&gt;object.item.",
                            mime_type_to_text(entries[i]->mime->type),
                            "Item&lt;/upnp:class&gt;");

            chunk_printf(h->st, "&lt;res size=\"%" PRIu64 "\" ", entries[i]->size);

            CHUNK_PRINT_ALL(h->st,
                            "protocolInfo=\"http-get:*:",
                            mime_type_to_text(entries[i]->mime->type),
                            "/",
                            entries[i]->mime->sub_type,
                            ":DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS="
                            "01700000000000000000000000000000\"&gt;http://",
                            get_interface_ip_str(h->iface),
                            listening_port_str,
                            "/MediaItems/",
                            url_escaped_dirpath,
                            "/", url_escaped_filename, "&lt;/res&gt;&lt;/item&gt;");

            if (url_escaped_filename != entries[i]->name)
                free((void *)url_escaped_filename);
        }
        if (xml_escaped_filename != entries[i]->name)
            free((void *)xml_escaped_filename);
    }

    if (url_escaped_dirpath != h->remote_dirpath)
        free((void *)url_escaped_dirpath);
    if (xml_escaped_dirpath != h->remote_dirpath)
        free((void *)xml_escaped_dirpath);

    chunk_printf(h->st, "&lt;/DIDL-Lite&gt;</Result>\n"
                 "<NumberReturned>%u</NumberReturned>\n"
                 "<TotalMatches>%u</TotalMatches>\n"
                 "<UpdateID>0</UpdateID>"
                 "</u:BrowseResponse>", number_returned, total_matches);

    chunk_print(h->st, afterbody);

    chunk_print_end(h->st);
}

void browse_content_directory(struct upnphttp *h)
{
    if (h->requested_count < 1)
        h->requested_count = -1;

    if (!h->remote_dirpath)
    {
        soap_error(h, 402, "Invalid Args - RemoteDirpath");
        return;
    }

    // a dirpath of '0' is an alias for the root dir
    if (strcmp("0", h->remote_dirpath) == 0)
        h->remote_dirpath[0] = '\0';

    // url unescape first
    url_unescape(h->remote_dirpath);

    // remote_dirpath is the raw filename
    xml_unescape(h->remote_dirpath);

    // get the directory listing
    unsigned int total_file_count;

    content_entry **entries = get_directory_listing(h, &total_file_count);

    if (!entries)
        return;

    print_xml_directory_listing(h, entries, h->requested_count, total_file_count);

    for (int i = 0; i < h->requested_count; i++)
        free(entries[i]);
    free(entries);
}

void unsupported_soap_action(struct upnphttp *h)
{
    soap_error(h, 708, "Unsupported Action");
}

void invalid_soap_action(struct upnphttp *h)
{
    soap_error(h, 401, "Invalid Action");
}
