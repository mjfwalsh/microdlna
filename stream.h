#pragma once
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

#define BUFFER_SIZE 1024

#include <stdio.h>

struct stream
{
    FILE *fh;
    char buf[BUFFER_SIZE];
    int pos;
};

struct stream *sdopen(int sd);

int sdclose(struct stream *s);

int stream_printf(struct stream *f, const char *fmt, ...)
__attribute__((__format__(__printf__, 2, 3)));

size_t stream_write(const void *restrict ptr, size_t nitems, struct stream *st);

void stream_flush(struct stream *s);

/* send http chunks */
void chunk_printf(struct stream *f, const char *fmt, ...)
__attribute__((__format__(__printf__, 2, 3)));

void chunk_print_len(struct stream *s, const char *text, int len);

void chunk_print(struct stream *f, const char *text);

void _chunk_print_all(struct stream *f, const char *first, ...);

#define CHUNK_PRINT_ALL(...) _chunk_print_all(__VA_ARGS__, (const char *)0);

int chunk_print_end(struct stream *f);
