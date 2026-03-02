/*
 *
 * This file is part of MicroDLNA:
 * Copyright (c) 2025, Michael Walsh
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

#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "xmlregex.h"
#include "upnphttp.h"
#include "utils.h"
#include "stream.h"

// Instead of parsing the xml we search it for name value pairs using
// the following regex:

// <([A-Za-z]+)[ \t\r\n][^>]>([^<]*)</$1[ \t\r\n]


#define BUF_SIZE 1024
#define ELE_NAME_SIZE 20
#define ELE_VALUE_SIZE (BUF_SIZE + 1)

enum ChunkMode
{
    FIRST_CHUNK,
    MIDDLE_CHUNK,
    NO_CHUNK,
};

struct cursor
{
    struct stream *st;
    int bytes_left;
    int chunked;
    int total_read;
    int error;
};


static inline int is_white_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static inline int is_greater_than_sign_or_white_space(char c)
{
    return c == ' ' || c == '>' || c == '\t' || c == '\r' || c == '\n';
}

static char read_char(struct cursor *xml)
{
    char buf[4];

    if (xml->bytes_left == 0)
    {
        switch (xml->chunked)
        {
        // we have finished reading the Content-Length
        case NO_CHUNK:
            return '\0';

        // look for the CRLF at the end of the chunk
        case MIDDLE_CHUNK:
            if (stream_read(&buf[0], 2, xml->st) != 2
                    || buf[0] != '\r' || buf[1] != '\n')
                goto error;
            break;

        // first chunk
        default:
        case FIRST_CHUNK:
            xml->chunked = MIDDLE_CHUNK;
            break;
        }

        // read hex number (max 3 chars) followed by CR
        int i = 0;
        while (i < 4)
        {
            if (stream_read(&buf[i], 1, xml->st) != 1)
                goto error;
            if (buf[i] == '\r')
                break;
            i++;
        }
        if (i == 4)
            goto error;

        char *endptr;
        int chunklen = strtoll(&buf[0], &endptr, 16);

        // validate number and check for trailing LF
        if (chunklen < 0 || *endptr != '\r'
                || stream_read(&buf[0], 1, xml->st) != 1
                || buf[0] != '\n')
            goto error;

        // trailing chunk
        if (chunklen == 0)
        {
            if (stream_read(&buf[0], 2, xml->st) != 2
                    || buf[0] != '\r' || buf[1] != '\n')
                goto error;

            return '\0';
        }

        // check total POST lengh
        xml->total_read += chunklen;
        if (xml->total_read > MAX_POST_SIZE)
            goto error;

        xml->bytes_left = chunklen;
    }

    xml->bytes_left--;
    if (stream_read(&buf[0], 1, xml->st) == 1 && buf[0] != '\0')
        return buf[0];

error:
    xml->error = 1;
    return '\0';
}

static void process_name_value_pair(struct upnphttp *h, const char *name,
                                    const char *value)
{
    if (strcmp(name, "ObjectID") == 0 || strcmp(name, "ContainerID") == 0)
    {
        if (h->remote_dirpath == NULL)
            h->remote_dirpath = safe_strdup(value);
    }
    else if (strcmp(name, "StartingIndex") == 0)
    {
        int indx = atoi(value);
        if (indx > 0)
            h->starting_index = indx;
    }
    else if (strcmp(name, "RequestedCount") == 0)
    {
        h->requested_count = atoi(value);
    }
}

int process_post_content(struct upnphttp *h)
{
    int i;
    char c;
    char ele_name[ELE_NAME_SIZE + 1];
    char ele_value[ELE_VALUE_SIZE];

    struct cursor xml;

    xml.st = h->st;
    xml.total_read = 0;
    xml.error = 0;
    if (h->reqflags & FLAG_CHUNKED)
    {
        xml.chunked = FIRST_CHUNK;
        xml.bytes_left = 0;
    }
    else
    {
        xml.chunked = NO_CHUNK;
        xml.bytes_left = h->data_len;
    }

    c = read_char(&xml);

    while (c != '\0')
    {
        // search for the next element
        while (c != '<' && c != '\0')
            c = read_char(&xml);
        if (c == '\0')
            break;
        c = read_char(&xml);

new_tag:
        // only accept letters as element names (ignore colons)
        i = 0;
        while (i < ELE_NAME_SIZE && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        {
            ele_name[i++] = c;
            c = read_char(&xml);
        }
        if (i == 0 || i == ELE_NAME_SIZE || !is_greater_than_sign_or_white_space(c))
            continue;

        ele_name[i] = '\0';

        // ignore the rest of the element tag (if any)
        while (c != '>' && c != '\0')
            c = read_char(&xml);
        if (c == '\0')
            break;
        c = read_char(&xml);

        // jump over any leading whitespace in the value
        while (is_white_space(c))
            c = read_char(&xml);

        // parse the value (zero-length is fine), leave room for '\0'
        i = 0;
        while (i < ELE_VALUE_SIZE - 1 && c != '<' && c != '\0')
        {
            ele_value[i++] = c;
            c = read_char(&xml);
        }
        ele_value[i] = '\0';
        if (i == ELE_VALUE_SIZE - 1 || c == '\0')
            continue;

        // right trim
        do
        {
            ;
        } while (i > 0 && is_white_space(ele_value[--i]));
        ele_value[i + 1] = '\0';

        // validate the closing xml tag
        c = read_char(&xml);
        if (c != '/')
            goto new_tag;

        c = read_char(&xml);
        const char *p = &ele_name[0];
        while (*p != '\0' && c == *p)
        {
            c = read_char(&xml);
            p++;
        }
        if (*p == '\0' && is_greater_than_sign_or_white_space(c))
        {
            // we have a result
            process_name_value_pair(h, ele_name, ele_value);
        }
    }

    return !xml.error;
}
