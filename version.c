/* MicroDLNA project
 *
 * https://github.com/mjfwalsh/microdlna
 *
 * Copyright (C) 2025  Michael J. Walsh
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

#include "upnphttp.h"
#include "version.h"
#include "version_info.h"
#include "log.h"

void print_version()
{
    printf("minidlna - lightweight, stateless DLNA/UPnP-AV server\n");
    printf("        Build date: %s\n", VERSION_DATE);
    printf("        Version   : %s (%s)\n", MICRODLNA_VERSION, VERSION_HASH);
    printf("        Branch    : %s\n", VERSION_BRANCH);
    printf("        Repository: %s\n", VERSION_REPO);
}

void log_short_version()
{
    PRINT_LOG(E_INFO, "Starting MicroDLNA version  %s (%s).\n", MICRODLNA_VERSION, VERSION_HASH);
}

const char *get_microdlna_version()
{
    return VERSION_HASH;
}
