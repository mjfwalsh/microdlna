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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mime.h"

static const struct ext_info types[] = {
    { "3ds", M_IMAGE, "x-3ds" },
    { "3g2", M_VIDEO, "3gpp2" },
    { "3gp", M_VIDEO, "3gpp" },
    { "aac", M_AUDIO, "x-aac" },
    { "adp", M_AUDIO, "adpcm" },
    { "aif", M_AUDIO, "x-aiff" },
    { "aifc", M_AUDIO, "x-aiff" },
    { "aiff", M_AUDIO, "x-aiff" },
    { "asf", M_VIDEO, "x-ms-asf" },
    { "asx", M_VIDEO, "x-ms-asf" },
    { "au", M_AUDIO, "basic" },
    { "avi", M_VIDEO, "x-msvideo" },
    { "bmp", M_IMAGE, "bmp" },
    { "btif", M_IMAGE, "prs.btif" },
    { "caf", M_AUDIO, "x-caf" },
    { "cgm", M_IMAGE, "cgm" },
    { "cmx", M_IMAGE, "x-cmx" },
    { "dif", M_VIDEO, "x-dv" },
    { "djv", M_IMAGE, "vnd.djvu" },
    { "djvu", M_IMAGE, "vnd.djvu" },
    { "dra", M_AUDIO, "vnd.dra" },
    { "dsd", M_AUDIO, "x-dsd" },
    { "dts", M_AUDIO, "vnd.dts" },
    { "dtshd", M_AUDIO, "vnd.dts.hd" },
    { "dv", M_VIDEO, "x-dv" },
    { "dvb", M_VIDEO, "vnd.dvb.file" },
    { "dwg", M_IMAGE, "vnd.dwg" },
    { "dxf", M_IMAGE, "vnd.dxf" },
    { "eol", M_AUDIO, "vnd.digital-winds" },
    { "f4v", M_VIDEO, "x-f4v" },
    { "fbs", M_IMAGE, "vnd.fastbidsheet" },
    { "fh", M_IMAGE, "x-freehand" },
    { "fh4", M_IMAGE, "x-freehand" },
    { "fh5", M_IMAGE, "x-freehand" },
    { "fh7", M_IMAGE, "x-freehand" },
    { "fhc", M_IMAGE, "x-freehand" },
    { "flac", M_AUDIO, "x-flac" },
    { "fli", M_VIDEO, "x-fli" },
    { "flv", M_VIDEO, "x-flv" },
    { "fpx", M_IMAGE, "vnd.fpx" },
    { "fst", M_IMAGE, "vnd.fst" },
    { "fvt", M_VIDEO, "vnd.fvt" },
    { "g3", M_IMAGE, "g3fax" },
    { "gif", M_IMAGE, "gif" },
    { "h261", M_VIDEO, "h261" },
    { "h263", M_VIDEO, "h263" },
    { "h264", M_VIDEO, "h264" },
    { "ico", M_IMAGE, "x-icon" },
    { "ief", M_IMAGE, "ief" },
    { "jp2", M_IMAGE, "jp2" },
    { "jpe", M_IMAGE, "jpeg" },
    { "jpeg", M_IMAGE, "jpeg" },
    { "jpg", M_IMAGE, "jpeg" },
    { "jpgm", M_VIDEO, "jpm" },
    { "jpgv", M_VIDEO, "jpeg" },
    { "jpm", M_VIDEO, "jpm" },
    { "kar", M_AUDIO, "midi" },
    { "ktx", M_IMAGE, "ktx" },
    { "lvp", M_AUDIO, "vnd.lucent.voice" },
    { "m1v", M_VIDEO, "mpeg" },
    { "m2a", M_AUDIO, "mpeg" },
    { "m2v", M_VIDEO, "mpeg" },
    { "m3a", M_AUDIO, "mpeg" },
    { "m3u", M_AUDIO, "x-mpegurl" },
    { "m4a", M_AUDIO, "mp4a-latm" },
    { "m4p", M_AUDIO, "mp4a-latm" },
    { "m4u", M_VIDEO, "vnd.mpegurl" },
    { "m4v", M_VIDEO, "x-m4v" },
    { "mac", M_IMAGE, "x-macpaint" },
    { "mdi", M_IMAGE, "vnd.ms-modi" },
    { "mid", M_AUDIO, "midi" },
    { "midi", M_AUDIO, "midi" },
    { "mj2", M_VIDEO, "mj2" },
    { "mjp2", M_VIDEO, "mj2" },
    { "mk3d", M_VIDEO, "x-matroska" },
    { "mka", M_AUDIO, "x-matroska" },
    { "mks", M_VIDEO, "x-matroska" },
    { "mkv", M_VIDEO, "x-matroska" },
    { "mmr", M_IMAGE, "vnd.fujixerox.edmics-mmr" },
    { "mng", M_VIDEO, "x-mng" },
    { "mov", M_VIDEO, "quicktime" },
    { "movie", M_VIDEO, "x-sgi-movie" },
    { "mp2", M_AUDIO, "mpeg" },
    { "mp2a", M_AUDIO, "mpeg" },
    { "mp3", M_AUDIO, "mpeg" },
    { "mp4", M_VIDEO, "mp4" },
    { "mp4a", M_AUDIO, "mp4" },
    { "mp4v", M_VIDEO, "mp4" },
    { "mpe", M_VIDEO, "mpeg" },
    { "mpeg", M_VIDEO, "mpeg" },
    { "mpg", M_VIDEO, "mpeg" },
    { "mpg4", M_VIDEO, "mp4" },
    { "mpga", M_AUDIO, "mpeg" },
    { "mxu", M_VIDEO, "vnd.mpegurl" },
    { "npx", M_IMAGE, "vnd.net-fpx" },
    { "oga", M_AUDIO, "ogg" },
    { "ogg", M_AUDIO, "ogg" },
    { "ogv", M_VIDEO, "ogg" },
    { "pbm", M_IMAGE, "x-portable-bitmap" },
    { "pcm", M_AUDIO, "L16" },
    { "pct", M_IMAGE, "x-pict" },
    { "pcx", M_IMAGE, "x-pcx" },
    { "pgm", M_IMAGE, "x-portable-graymap" },
    { "pic", M_IMAGE, "x-pict" },
    { "pict", M_IMAGE, "pict" },
    { "png", M_IMAGE, "png" },
    { "pnm", M_IMAGE, "x-portable-anymap" },
    { "pnt", M_IMAGE, "x-macpaint" },
    { "pntg", M_IMAGE, "x-macpaint" },
    { "ppm", M_IMAGE, "x-portable-pixmap" },
    { "psd", M_IMAGE, "vnd.adobe.photoshop" },
    { "pya", M_AUDIO, "vnd.ms-playready.media.pya" },
    { "pyv", M_VIDEO, "vnd.ms-playready.media.pyv" },
    { "qt", M_VIDEO, "quicktime" },
    { "qti", M_IMAGE, "x-quicktime" },
    { "qtif", M_IMAGE, "x-quicktime" },
    { "ra", M_AUDIO, "x-pn-realaudio" },
    { "ram", M_AUDIO, "x-pn-realaudio" },
    { "ras", M_IMAGE, "x-cmu-raster" },
    { "rgb", M_IMAGE, "x-rgb" },
    { "rip", M_AUDIO, "vnd.rip" },
    { "rlc", M_IMAGE, "vnd.fujixerox.edmics-rlc" },
    { "rmi", M_AUDIO, "midi" },
    { "rmp", M_AUDIO, "x-pn-realaudio-plugin" },
    { "s3m", M_AUDIO, "s3m" },
    { "sgi", M_IMAGE, "sgi" },
    { "sid", M_IMAGE, "x-mrsid-image" },
    { "sil", M_AUDIO, "silk" },
    { "smv", M_VIDEO, "x-smv" },
    { "snd", M_AUDIO, "basic" },
    { "spx", M_AUDIO, "ogg" },
    { "srt", M_TEXT, "srt" },
    { "sub", M_IMAGE, "vnd.dvb.subtitle" },
    { "svg", M_IMAGE, "svg+xml" },
    { "svgz", M_IMAGE, "svg+xml" },
    { "tga", M_IMAGE, "x-tga" },
    { "tif", M_IMAGE, "tiff" },
    { "tiff", M_IMAGE, "tiff" },
    { "ts", M_VIDEO, "mp2t" },
    { "uva", M_AUDIO, "vnd.dece.audio" },
    { "uvg", M_IMAGE, "vnd.dece.graphic" },
    { "uvh", M_VIDEO, "vnd.dece.hd" },
    { "uvi", M_IMAGE, "vnd.dece.graphic" },
    { "uvm", M_VIDEO, "vnd.dece.mobile" },
    { "uvp", M_VIDEO, "vnd.dece.pd" },
    { "uvs", M_VIDEO, "vnd.dece.sd" },
    { "uvu", M_VIDEO, "vnd.uvvu.mp4" },
    { "uvv", M_VIDEO, "vnd.dece.video" },
    { "uvva", M_AUDIO, "vnd.dece.audio" },
    { "uvvg", M_IMAGE, "vnd.dece.graphic" },
    { "uvvh", M_VIDEO, "vnd.dece.hd" },
    { "uvvi", M_IMAGE, "vnd.dece.graphic" },
    { "uvvm", M_VIDEO, "vnd.dece.mobile" },
    { "uvvp", M_VIDEO, "vnd.dece.pd" },
    { "uvvs", M_VIDEO, "vnd.dece.sd" },
    { "uvvu", M_VIDEO, "vnd.uvvu.mp4" },
    { "uvvv", M_VIDEO, "vnd.dece.video" },
    { "viv", M_VIDEO, "vnd.vivo" },
    { "vob", M_VIDEO, "x-ms-vob" },
    { "wav", M_AUDIO, "x-wav" },
    { "wax", M_AUDIO, "x-ms-wax" },
    { "wbmp", M_IMAGE, "vnd.wap.wbmp" },
    { "wdp", M_IMAGE, "vnd.ms-photo" },
    { "weba", M_AUDIO, "webm" },
    { "webm", M_VIDEO, "webm" },
    { "webp", M_IMAGE, "webp" },
    { "wm", M_VIDEO, "x-ms-wm" },
    { "wma", M_AUDIO, "x-ms-wma" },
    { "wmv", M_VIDEO, "x-ms-wmv" },
    { "wmx", M_VIDEO, "x-ms-wmx" },
    { "wvx", M_VIDEO, "x-ms-wvx" },
    { "xbm", M_IMAGE, "x-xbitmap" },
    { "xif", M_IMAGE, "vnd.xiff" },
    { "xm", M_AUDIO, "xm" },
    { "xpm", M_IMAGE, "x-xpixmap" },
    { "xwd", M_IMAGE, "x-xwindowdump" }
};

static const int item_size = sizeof(struct ext_info);
static const int item_count = sizeof(types) / sizeof(struct ext_info);

static int cmp(const void *aa, const void *bb)
{
    const char *a = (const char *)aa;
    const struct ext_info *x = (const struct ext_info *)bb;
    const char *b = (const char *)x->ext;

    return strcmp(a, b);
}

static void lowercase(const char *p, char *transformed)
{
    while (*p != '\0')
    {
        if (*p >= 'A' && *p <= 'Z')
            *transformed++ = *p++ + 32;
        else
            *transformed++ = *p++;
    }
    *transformed = '\0';
}

static struct ext_info *get_mime_type_from_ext(const char *old_ext)
{
    char ext[6];

    lowercase(old_ext, &ext[0]);

    // search
    return bsearch(ext, types, item_count, item_size, cmp);
}

const char *mime_type_to_text(enum MimeType main_type)
{
    switch (main_type)
    {
    case M_VIDEO:
        return "video";

    case M_AUDIO:
        return "audio";

    case M_IMAGE:
        return "image";

    case M_TEXT:
        return "text";

    default:
        return "";
    }
}


struct ext_info *get_mime_type(const char *filename)
{
    int len = strlen(filename);

    for (int i = len - 1; i > len - 7; i--)
        if (filename[i] == '.')
            return get_mime_type_from_ext(filename + i + 1);

    return NULL;
}
