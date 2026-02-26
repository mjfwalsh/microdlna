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

#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>

#include "upnphttp.h"
#include "globalvars.h"
#include "dirlist.h"
#include "log.h"
#include "utils.h"
#include "mime.h"
#include "mediadir.h"

// deline to serve or sort this many files
#define MAX_FILE_LIMIT 10240

static int content_entry_compare(const void *aa, const void *bb)
{
    const content_entry *a = *((const content_entry *const *)aa);
    const content_entry *b = *((const content_entry *const *)bb);

    if (a->type != b->type)
        return a->type - b->type;

    return strcasecmp(a->name, b->name);
}


void free_directory_listing(directory_listing *dl)
{
    for (int i = 0; i < dl->length; i++)
        free(dl->entries[i]);
    free(dl->entries);

    dl->length = 0;
    dl->entries = NULL;
}

int get_directory_listing(struct upnphttp *h, directory_listing *dl)
{
    if (h->requested_count < 1 || h->requested_count > MAX_FILE_LIMIT)
        h->requested_count = -1;

    if (h->starting_index < 0 || h->starting_index > MAX_FILE_LIMIT)
        h->starting_index = 0;

    // check for funny file paths
    if (!sanitise_path(h->remote_dirpath))
    {
        PRINT_LOG(E_DEBUG,
                  "Browsing ContentDirectory failed: addressing out of media dir: ObjectID='%s'\n",
                  h->remote_dirpath);
        send_http_response(h, HTTP_FORBIDDEN_403);
        return 0;
    }

    PRINT_LOG(E_DEBUG, "Browsing ContentDirectory:\n"
              " * ObjectID: %s\n"
              " * Count: %d\n"
              " * StartingIndex: %d\n", h->remote_dirpath, h->requested_count,
              h->starting_index);

    const char *rel_dir = h->remote_dirpath[0] == '\0' ? "." : h->remote_dirpath;
    DIR *dir;

    if (chdir_to_media_dir() != 0 || !(dir = opendir(rel_dir)))
    {
        PRINT_LOG(E_INFO, "Browsing ContentDirectory failed: %s/%s\n", media_dir,
                  h->remote_dirpath);
        send_http_response(h, HTTP_SERVICE_UNAVAILABLE_503);
        return 0;
    }

    int allocated_entries = 256;
    dl->entries = (content_entry **)safe_malloc(allocated_entries * sizeof(content_entry *));
    dl->length = 0;

    struct dirent *de;
    int fd = dirfd(dir);
    while ((de = readdir(dir)))
    {
        // skip all hidden files
        if (de->d_name[0] == '.' || de->d_name[0] == '$')
            continue;

        struct stat st;
        if (fstatat(fd, de->d_name, &st, 0) != 0)
            continue;

        const struct ext_info *mime = NULL;
        filetype type;
        if (S_ISDIR(st.st_mode))
        {
            // check if we can enter and read that folder
            if (faccessat(fd, de->d_name, R_OK | X_OK, 0))
                continue;

            type = T_DIR;
            st.st_size = 0;
        }
        else if (S_ISREG(st.st_mode))
        {
            mime = get_mime_type(de->d_name);
            if (!mime)
                continue;

            type = T_FILE;
        }
        else
        {
            continue;
        }

        // allocate file entry
        size_t name_size = strlen(de->d_name) + 1;
        dl->entries[dl->length] =
            (content_entry *)malloc(sizeof(content_entry) + name_size);
        if (!dl->entries[dl->length])
        {
            free_directory_listing(dl);
            closedir(dir);
            send_http_response(h, HTTP_INSUFFICIENT_STORAGE_507);
            return 0;
        }

        dl->entries[dl->length]->type = type;
        dl->entries[dl->length]->size = st.st_size;
        dl->entries[dl->length]->mime = mime;
        strxcpy(dl->entries[dl->length]->name, de->d_name, name_size);

        // entry is valid, so allocate a new one and step the counter
        dl->length++;
        if (dl->length >= allocated_entries)
        {
            allocated_entries += 128;

            content_entry **p;
            if(allocated_entries < MAX_FILE_LIMIT
                && (p = realloc(dl->entries, allocated_entries * sizeof(content_entry *))))
            {
                dl->entries = p;
            }
            else
            {
                free_directory_listing(dl);
                closedir(dir);
                send_http_response(h, HTTP_INSUFFICIENT_STORAGE_507);
                return 0;
            }
        }
    }
    closedir(dir);

    if (dl->length == 0)
    {
        free_directory_listing(dl);
        h->requested_count = 0;
        return 1;
    }

    safe_realloc((void **)&dl->entries, dl->length * sizeof(content_entry *));

    qsort(dl->entries, dl->length, sizeof(content_entry *), content_entry_compare);

    if (h->requested_count == -1
        || h->starting_index + h->requested_count > dl->length)
    {
        h->requested_count = dl->length - h->starting_index;
        if (h->requested_count < 0)
            h->requested_count = 0;
    }

    return 1;
}
