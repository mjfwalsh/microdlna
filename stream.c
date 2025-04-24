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

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include "stream.h"
#include "utils.h"

struct stream *sdopen(int sd)
{
    FILE *fh = fdopen(sd, "w");

    if (!fh)
        return NULL;

    struct stream *s = safe_malloc(sizeof(struct stream));
    s->fh = fh;
    s->pos = 0;
    return s;
}

// functions to write http chunks

static void stream_clear(struct stream *s)
{
    if (s->pos <= 0)
        return;

    fprintf(s->fh, "%X\r\n", s->pos);
    fwrite(s->buf, 1, s->pos, s->fh);
    fputs("\r\n", s->fh);
    s->pos = 0;
}

void chunk_printf(struct stream *s, const char *fmt, ...)
{
    stream_clear(s);

    va_list va;
    va_start(va, fmt);
    int bytes_written = vsnprintf(s->buf, BUFFER_SIZE, fmt, va);
    va_end(va);

    if (bytes_written <= 0)
        return;

    fprintf(s->fh, "%X\r\n%s\r\n", bytes_written, s->buf);
}

void chunk_print(struct stream *s, const char *text)
{
    while (*text != '\0')
    {
        if (s->pos == BUFFER_SIZE)
            stream_clear(s);

        s->buf[s->pos] = *text;
        s->pos++;
        text++;
    }
}

void chunk_print_len(struct stream *s, const char *text, int len)
{
    while (*text != '\0' && len > 0)
    {
        if (s->pos == BUFFER_SIZE)
            stream_clear(s);

        s->buf[s->pos] = *text;
        s->pos++;
        text++;
        len--;
    }
}

void _chunk_print_all(struct stream *s, const char *first, ...)
{
    va_list ap;

    va_start(ap, first);
    for (const char *arg = first; arg; arg = va_arg(ap, const char *))
        chunk_print(s, arg);

    va_end(ap);
}

int chunk_print_end(struct stream *s)
{
    stream_clear(s);
    return fputs("0\r\n\r\n", s->fh);
}

// standard io funcs

size_t stream_write(const void *restrict ptr, size_t nitems, struct stream *s)
{
    return fwrite(ptr, 1, nitems, s->fh);
}

int stream_printf(struct stream *s, const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);

    int bytes_written = vfprintf(s->fh, fmt, va);

    va_end(va);

    return bytes_written;
}

void stream_flush(struct stream *s)
{
    stream_clear(s);
    fflush(s->fh);
}

int sdclose(struct stream *s)
{
    stream_clear(s);
    int r = fclose(s->fh);
    free(s);
    return r;
}
