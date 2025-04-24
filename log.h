#pragma once
/* MicroDLNA media server
 *
 * This file is part of MicroDLNA.
 *
 * Copyright (c) 2025, 
 * All rights reserved.
 *
 * Based on the MiniDLNA project:
 * Copyright (C) 2008-2010 NETGEAR, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdlib.h>

enum LogLevel
{
    E_OFF,
    E_FATAL,
    E_INFO,
    E_ERROR,
    E_DEBUG,
};

extern int log_level;

void log_err(enum LogLevel level, const char *fname, int lineno,
             char *fmt, ...) __attribute__((__format__(__printf__, 4, 5)));

void set_debug_level(const char *new_level);

#define PRINT_LOG(level, fmt, arg ...) \
        do { \
            if (level <= log_level) \
                log_err(level, __FILE__, __LINE__, fmt, ## arg); \
        } while (0)

#define EXIT_ERROR(fmt, arg ...) \
        do { \
            log_err(E_FATAL, __FILE__, __LINE__, fmt, ## arg); \
            exit(1); \
        } while (0)
