/* MicroDLNA media server
 *
 * This file is part of MicroDLNA.
 *
 * Copyright (c) 2016, Gabor Simon
 * All rights reserved.
 *
 * With alternations by 
 * Michael J.Walsh
 * Copyright (c) 2025
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
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "globalvars.h"
#include "log.h"

// default log level
int log_level = E_INFO;

static const char *level_name[] = {
    "off",                      // E_OFF
    "fatal",                    // E_FATAL
    "info",                     // E_INFO
    "error",                    // E_ERROR
    "debug",                    // E_DEBUG
    0
};

void log_err(enum LogLevel level, const char *fname, int lineno, char *fmt, ...)
{
    // timestamp
    if (!mode_systemd)
    {
        struct tm tm;
        time_t t = time(NULL);
        localtime_r(&t, &tm);
        fprintf(stderr, "[%04d/%02d/%02d %02d:%02d:%02d] ",
               tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
               tm.tm_hour, tm.tm_min, tm.tm_sec);
    }

    fprintf(stderr, "%s:%d: %s: ", fname, lineno, level_name[level]);

    // user log
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void set_debug_level(const char *new_level)
{
    for (int i = 0; level_name[i]; i++)
    {
        if (strcmp(level_name[i], new_level) == 0)
        {
            log_level = i;
            return;
        }
    }
    EXIT_ERROR("Invalid log level: %s\n", new_level);
}
