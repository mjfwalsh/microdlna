/* MicroDLNA media server
 * Copyright (C) 2025  Justin Maggard
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
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "utils.h"
#include "log.h"

// stolen from glibc
const char *inet_ntoa_ts(struct in_addr in)
{
    static __thread char buf[16];

    inet_ntop(AF_INET, &in.s_addr, buf, 16);
    return buf;
}

// Every char except the following needs to be escaped
//  *+-./01234567689@ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz~
static int needs_escaping(char c)
{
    switch (c)
    {
    case '*':
    case '+':
    case '-' ... '9':
    case '@' ... 'Z':
    case '_':
    case 'a' ... 'z':
    case '~':
        return 0;

    default:
        return 1;
    }
}

const char *url_escape(const char *input_string)
{
    int normal = 0;
    int to_escape = 0;

    for (int i = 0; input_string[i]; i++)
    {
        if (needs_escaping(input_string[i]))
            to_escape++;
        else
            normal++;
    }

    if (to_escape == 0)
        return input_string;

    char *escaped_string = safe_malloc(normal + 3 * to_escape + 1);

    char *p;
    for (p = escaped_string; *input_string; input_string++)
    {
        if (needs_escaping(*input_string))
            p += sprintf(p, "%%%02X", (unsigned char)*input_string);
        else
            *(p++) = *input_string;
    }
    *p = '\0';
    return escaped_string;
}

static int convert_hex(const char *s)
{
    int n = 0;

    for (int i = 0; i < 2; i++)
    {
        n <<= 4;
        if (s[i] >= 'A' && s[i] <= 'F')
            n += s[i] - 'A' + 10;
        else if (s[i] >= 'a' && s[i] <= 'f')
            n += s[i] - 'a' + 10;
        else if (s[i] >= '0' && s[i] <= '9')
            n += s[i] - '0';
        else
            return -1;
    }
    return n;
}

// does in-place conversion
void url_unescape(char *tag)
{
    char *r = tag;
    char *w = tag;

    while (*r != '\0')
    {
        if (*r == '+')
        {
            *w++ = ' ';
            r++;
        }
        else if (*r == '%')
        {
            int n = convert_hex(r + 1);
            if (n > -1)
            {
                if (n < 32 || n == 127)             // ignore control chars
                    *w++ = ' ';
                else
                    *w++ = (char)n;
                r += 3;
            }
            else if (*(r + 1) == '%')
            {
                *w++ = '%';
                r += 2;
            }
            else
            {
                *w++ = '%';
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

int sanitise_path(char *path)
{
    int i = 0;

    while (1)
    {
        while (path[i] == '/')
            i++;
        if (path[i] == '\0')
            break;

        if (path[i] == '.' && (path[i + 1] == '/' || path[i + 1] == '\0'))
        {
            // ignore single dot dirs
            path[i] = '/';
        }
        else if (path[i] == '.' && path[i + 1] == '.'
                 && (path[i + 2] == '/' || path[i + 2] == '\0'))
        {
            // write over two-dot dirs and ...
            path[i] = '/';
            path[i + 1] = '/';

            // find a previous directory to delete too (if any)
            int j = i - 1;
            while (j > -1 && path[j] == '/')
                j--;

            // if none, it's not a valid url
            if (j < 0)
                return 0;

            // write over it with slashes
            while (j > -1 && path[j] != '/')
                path[j--] = '/';
        }
        else
        {
            while (path[i] != '/' && path[i] != '\0')
                i++;
            if (path[i] == '\0')
                break;
        }
    }

    // '//+' - > '/'
    const char *r = path;
    char *w = path;

    while (*r == '/')
        r++;

    while (*r != '\0')
    {
        if (*r == '/')
            while (*(r + 1) == '/')
                r++;

        *w++ = *r++;
    }

    if (w > path && *(w - 1) == '/')
        w--;

    *w = '\0';
    return 1;
}

// "safe" memory functions
// aborting on a memory error is a bit of a cop-out but easier that trying to recover

void *safe_malloc(size_t size)
{
    void *ptr = malloc(size);
    if (!ptr)
        EXIT_ERROR("safe_malloc: allocation failed\n");
    return ptr;
}

void safe_realloc(void **ptr, size_t size)
{
    void *nptr = realloc(*ptr, size);
    if (size > 0 && !nptr)
        EXIT_ERROR("safe_realloc: allocation failed\n");
    *ptr = nptr;
}

char *safe_strdup(const char *s)
{
    int size = strlen(s) + 1;
    char *ptr = safe_malloc(size);

    memcpy(ptr, s, size);
    return ptr;
}
