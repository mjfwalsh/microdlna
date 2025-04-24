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

static int content_entry_compare(const void *aa, const void *bb)
{
    const content_entry *a = *((const content_entry *const *)aa);
    const content_entry *b = *((const content_entry *const *)bb);

    if (a->type != b->type)
        return a->type - b->type;

    return strcasecmp(a->name, b->name);
}


content_entry **get_directory_listing(struct upnphttp *h, unsigned int *file_count)
{
    // check for funny file paths
    if (!sanitise_path(h->remote_dirpath))
    {
        PRINT_LOG(E_DEBUG,
                  "Browsing ContentDirectory failed: addressing out of media dir: ObjectID='%s'\n",
                  h->remote_dirpath);
        send_http_response(h, HTTP_FORBIDDEN_403);
        return NULL;
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
        return NULL;
    }

    *file_count = 0;
    content_entry **entries;

    unsigned int allocated_entries = 256;
    entries = (content_entry **)safe_malloc(allocated_entries * sizeof(content_entry *));

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
        entries[*file_count] =
            (content_entry *)safe_malloc(sizeof(content_entry) + name_size);
        entries[*file_count]->type = type;
        entries[*file_count]->size = st.st_size;
        entries[*file_count]->mime = mime;
        strxcpy(entries[*file_count]->name, de->d_name, name_size);

        // entry is valid, so allocate a new one and step the counter
        (*file_count)++;
        if (*file_count >= allocated_entries)
        {
            allocated_entries += 128;
            safe_realloc((void **)&entries, allocated_entries * sizeof(content_entry *));
        }
    }
    closedir(dir);

    safe_realloc((void **)&entries, *file_count * sizeof(content_entry *));

    qsort(entries, *file_count, sizeof(content_entry *), content_entry_compare);

    if (h->starting_index >= *file_count)
    {
        h->starting_index = 0;
        h->requested_count = 0;
    }

    if (h->requested_count == -1 || h->starting_index + h->requested_count > *file_count)
        h->requested_count = *file_count - h->starting_index;

    // free any entries before the h->starting_index
    for (unsigned int i = 0; i < h->starting_index; i++)
        free(entries[i]);

    // free any entries after the end of the requested count
    for (unsigned int i = h->starting_index + h->requested_count; i < *file_count; i++)
        free(entries[i]);

    // shift used entries to the left
    if (h->starting_index > 0)
        for (int i = 0; i < h->requested_count; i++)
            entries[i] = entries[i + h->starting_index];

    safe_realloc((void **)&entries, h->requested_count * sizeof(content_entry *));

    return entries;
}
