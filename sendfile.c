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

#define MAX_BUFFER_SIZE 2147483647
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



void send_file(int socketfd, int sendfd, off_t offset, size_t end_offset)
{
    size_t send_size;
    off_t ret;

#if defined(HAVE_SYS_SENDFILE)
    static int try_sendfile = 1;

    if (try_sendfile)
    {
        while (offset <= end_offset)
        {
            send_size = end_offset - offset + 1;
            if (send_size > MAX_BUFFER_SIZE)
                send_size = MAX_BUFFER_SIZE;

            PRINT_LOG(E_DEBUG, "sendfile range %jd to %zu\n", (intmax_t)offset,
                      send_size);
            ret = sys_sendfile(socketfd, sendfd, &offset, send_size);
            if (ret == -1)
            {
                // a broken pipe isn't really an error
                if (errno == EPIPE)
                    break;

                PRINT_LOG(E_DEBUG, "sendfile error :: error no. %d\n", errno);
                /* If sendfile isn't supported on the filesystem, don't bother trying to use it again. */
                if (errno == EOVERFLOW || errno == EINVAL)
                    goto fallback;
                else if (errno != EAGAIN)
                    break;
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
        if (send_size > MIN_BUFFER_SIZE)
            send_size = MIN_BUFFER_SIZE;

        lseek(sendfd, offset, SEEK_SET);
        ret = read(sendfd, buf, send_size);
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
