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

// Instead of parsing the xml we search it for name value pairs using
// the following regex:

// <([A-Za-z]+)[ \t\r\n][^>]>([^<]*)</$1[ \t\r\n]


#define BUF_SIZE 1024

enum ChunkMode
{
    FIRST_CHUNK,
    MIDDLE_CHUNK,
    NO_CHUNK,
};

struct cursor
{
    int fd;
    int bytes_left;
    int chunked;
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
        case NO_CHUNK:
            return '\0';

        case MIDDLE_CHUNK:
            if (read(xml->fd, &buf[0], 2) != 2)
                return '\0';

            if (buf[0] != '\r' || buf[1] != '\n')
                return '\0';
            break;

        default:
        case FIRST_CHUNK:
            xml->chunked = MIDDLE_CHUNK;
            break;
        }

        // read chunk header
        int i = 0;
        while (i < 4)
        {
            if (read(xml->fd, &buf[i], 1) != 1)
                return '\0';
            if (buf[i] == '\r')
                break;
            i++;
        }
        if (i == 4)
            return '\0';

        char *endptr;
        int chunklen = strtoll(&buf[0], &endptr, 16);
        if (endptr == &buf[0] || *endptr != '\r')
            return '\0';

        if (read(xml->fd, &buf[0], 1) != 1 || buf[0] != '\n' || chunklen < 1
            || chunklen > 2048)
            return '\0';

        xml->bytes_left = chunklen;
    }

    xml->bytes_left--;
    if (read(xml->fd, &buf[0], 1) == 1)
        return buf[0];
    else
        return '\0';
}

static void process_name_value_pair(struct upnphttp *h, const char *name,
                                    const char *value)
{
    if (strcmp(name, "ObjectID") == 0 || strcmp(name, "ContainerID") == 0)
    {
        h->remote_dirpath = safe_strdup(value);
    }
    else if (strcmp(name, "StartingIndex") == 0)
    {
        int indx = atoi(value);
        if (indx > 0)
            h->starting_index = indx;
    }
    if (strcmp(name, "RequestedCount") == 0)
    {
        h->requested_count = atoi(value);
    }
}

void process_post_content(struct upnphttp *h)
{
    int i;
    char c;
    char ele_name[20];
    char ele_value[BUF_SIZE];

    struct cursor xml;

    xml.fd = h->fd;
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
        while (i < 20 && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        {
            ele_name[i++] = c;
            c = read_char(&xml);
        }
        if (i == 0 || i == 20 || !is_greater_than_sign_or_white_space(c))
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

        // parse the value (zero-length is fine)
        i = 0;
        while (i < BUF_SIZE && c != '<' && c != '\0')
        {
            ele_value[i++] = c;
            c = read_char(&xml);
        }
        if (i == BUF_SIZE || c == '\0')
            break;

        // set terminating null and right trim
        do
        {
            ele_value[i] = '\0';
        } while (i > 0 && is_white_space(ele_value[--i]));

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
}
