#pragma once
/* Utility functions
 *
 * Copyright (C) 2025, Michael J. Walsh
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

#include <string.h>
#include <netinet/in.h>

const char *inet_ntoa_ts(struct in_addr in);

static inline void strxcpy(char *dst, const char *src, size_t size)
{
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

const char *url_escape(const char *input);

void url_unescape(char *input);

int sanitise_path(char *path);

/* safe memory functions */
void *safe_malloc(size_t size);

void safe_realloc(void **ptr, size_t size);

char *safe_strdup(const char *s);
