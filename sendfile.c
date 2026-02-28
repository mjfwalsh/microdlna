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
#define SENDFILE_MAX_TRANSFER 2147483647
/* Fallback read/write buffer size (64 KiB): one allocation, decent throughput. */
#define BUFFER_SIZE 65536

#if defined(__linux__)

#define HAVE_SYS_SENDFILE

#include <sys/sendfile.h>

static inline int sys_sendfile(int sock, int sendfd, off_t *offset, off_t len)
{
    return sendfile(sock, sendfd, offset, (size_t)len);
}

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



void send_file(int socketfd, int sendfd, off_t offset, off_t end_offset)
{
    off_t send_size;
    off_t ret;

#if defined(HAVE_SYS_SENDFILE)
    static int try_sendfile = 1;

    if (try_sendfile)
    {
        while (offset <= end_offset)
        {
            send_size = end_offset - offset + 1;
            if (send_size > SENDFILE_MAX_TRANSFER)
                send_size = SENDFILE_MAX_TRANSFER;

            PRINT_LOG(E_DEBUG, "sendfile range %jd to %jd\n", (intmax_t)offset,
                      (intmax_t)send_size);
            ret = sys_sendfile(socketfd, sendfd, &offset, send_size);
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
    char *buf = safe_malloc(BUFFER_SIZE);

    while (offset <= end_offset)
    {
        send_size = end_offset - offset + 1;
        if (send_size > (off_t)BUFFER_SIZE)
            send_size = BUFFER_SIZE;

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
