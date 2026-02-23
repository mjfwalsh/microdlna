/* MicroDLNA media server
 * Copyright (c) 2016, Gabor Simon
 * All rights reserved.
 *
 * Based on the MiniDLNA project:
 * Copyright (C) 2013  NETGEAR
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

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

#include "sendfile.h"
#include "utils.h"
#include "log.h"

/* Max bytes per sendfile() call (2^31-1): keeps count in 32-bit range, avoids huge
 * single kernel transfers. Only used when HAVE_SYS_SENDFILE. */
#define MAX_BUFFER_SIZE 2147483647
/* Fallback read/write buffer size (64 KiB): one allocation, decent throughput. */
#define MIN_BUFFER_SIZE 65536

#if defined(__linux__)

#define HAVE_SYS_SENDFILE

#include <sys/sendfile.h>

#define sys_sendfile sendfile

#elif defined(__APPLE__)

#define HAVE_SYS_SENDFILE

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

static inline int sys_sendfile(int sock, int sendfd, off_t *offset, off_t len)
{
    int ret = sendfile(sendfd, sock, *offset, &len, NULL, 0);

    *offset += len;

    return ret;
}

#elif defined(__FreeBSD__) || defined(__NetBSD__)

#define HAVE_SYS_SENDFILE

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

static inline int sys_sendfile(int sock, int sendfd, off_t *offset, off_t len)
{
    size_t nbytes = len;
    int ret = sendfile(sendfd, sock, *offset, nbytes, NULL, &len, 0);

    *offset += len;

    return ret;
}

#endif



/* Skip sendfile for transfers larger than 2GB; use read/write to avoid
 * kernel/fs issues (e.g. sendfile returning 0 or -1 with large count). */
#define SENDFILE_MAX_TRANSFER ((off_t)2147483647)

void send_file(int socketfd, int sendfd, off_t offset, off_t end_offset)
{
    off_t send_size;
    off_t ret;

#if defined(HAVE_SYS_SENDFILE)
    static int try_sendfile = 1;
    off_t total = end_offset - offset + 1;

    if (try_sendfile && total <= SENDFILE_MAX_TRANSFER)
    {
        while (offset <= end_offset)
        {
            send_size = end_offset - offset + 1;
            if (send_size > MAX_BUFFER_SIZE)
                send_size = MAX_BUFFER_SIZE;

            PRINT_LOG(E_DEBUG, "sendfile range %jd to %jd\n", (intmax_t)offset,
                      (intmax_t)send_size);
            ret = sys_sendfile(socketfd, sendfd, &offset, (size_t)send_size);
            if (ret == -1)
            {
                /* Client closed connection */
                if (errno == EPIPE)
                    break;

                PRINT_LOG(E_DEBUG, "sendfile error :: error no. %d\n", errno);
                /* Fall back to read/write on any sendfile error. */
                goto fallback;
            }
            if (ret == 0)
            {
                /* No progress (e.g. socket buffer full or kernel quirk). Avoid infinite loop. */
                PRINT_LOG(E_DEBUG, "sendfile returned 0, falling back to read/write\n");
                goto fallback;
            }
        }
        return;

fallback:
        try_sendfile = 0;
    }
#endif

    /* Fall back to regular I/O */
    PRINT_LOG(E_DEBUG, "Falling back on regular I/O: error no. %d\n", errno);
    char *buf = safe_malloc(MIN_BUFFER_SIZE);

    while (offset <= end_offset)
    {
        send_size = end_offset - offset + 1;
        if (send_size > (off_t)MIN_BUFFER_SIZE)
            send_size = MIN_BUFFER_SIZE;

        lseek(sendfd, offset, SEEK_SET);
        ret = read(sendfd, buf, (size_t)send_size);
        if (ret == -1)
        {
            PRINT_LOG(E_DEBUG, "read error :: error no. %d\n", errno);
            if (errno == EAGAIN)
                continue;
            else
                break;
        }
        ret = write(socketfd, buf, ret);
        if (ret == -1)
        {
            PRINT_LOG(E_DEBUG, "write error :: error no. %d\n", errno);
            if (errno == EAGAIN)
                continue;
            else
                break;
        }
        offset += ret;
    }
    free(buf);
}
