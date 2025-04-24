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
#include <string.h>
#include <pthread.h>

#include "threads.h"
#include "globalvars.h"
#include "log.h"

static int active_threads = 0;

static pthread_mutex_t lock;
static pthread_attr_t thread_attrs;

void decrement_thread_count(void)
{
    pthread_mutex_lock(&lock);
    active_threads--;
    PRINT_LOG(E_DEBUG, "ending thread: total threads: %d\n", active_threads);
    pthread_mutex_unlock(&lock);
}

int create_thread(void *(*start_routine)(void *), void *arg)
{
    pthread_mutex_lock(&lock);

    if (active_threads >= max_connections)
    {
        PRINT_LOG(E_ERROR, "Exceeded max connections [%d], not threading\n",
                  max_connections);
        pthread_mutex_unlock(&lock);
        return -1;
    }

    pthread_t thr;
    int r = pthread_create(&thr, &thread_attrs, start_routine, arg);
    if (r == 0)
    {
        active_threads++;
        PRINT_LOG(E_DEBUG, "creating thread: total threads: %d\n", active_threads);
    }
    else
    {
        PRINT_LOG(E_ERROR, "pthread_create failed: %d\n", r);
        r = -1;
    }

    pthread_mutex_unlock(&lock);

    return r;
}

void init_threads(void)
{
    pthread_mutex_init(&lock, NULL);
    pthread_attr_init(&thread_attrs);
    pthread_attr_setdetachstate(&thread_attrs, PTHREAD_CREATE_DETACHED);
}
