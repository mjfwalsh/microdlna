/* MiniUPnP project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 *
 * Copyright (c) 2016, Gabor Simon
 * All rights reserved.
 *
 * With alternations by 
 * Michael J.Walsh
 * Copyright (c) 2025
 *
 * Based on the MiniDLNA project:
 * Copyright (c) 2006-2008, Thomas Bernard
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
#include <unistd.h>

#include "globalvars.h"
#include "utils.h"
#include "getifaddr.h"
#include "stream.h"
#include "upnpdescgen.h"
#include "microdlnapath.h"
#include "version.h"

/* Manufacturer */
#define ROOTDEV_MANUFACTURER "Michael J. Walsh"

/* Manufacturer URL */
#define ROOTDEV_MANUFACTURERURL "https://github.com/mjfwalsh/microdlna"

/* Model description */
#define ROOTDEV_MODELDESCRIPTION "MicroDLNA"

/* Model name */
#define ROOTDEV_MODELNAME "MicroDLNA Media Server"

/* Model number */
#define ROOTDEV_SERIALNUMBER "00000000"

/* for the root description
 * The child list reference is stored in "data" member using the
 * INITHELPER macro with index/nchild always in the
 * same order, whatever the endianness */
struct xml_elt
{
    const char *name;           /* begin with '/' if no child */
    union
    {                           /* Value */
        const char *value;
        int children;
        void (*callback)(struct stream *);
    };
};

struct action
{
    const char *name;
    const struct argument *args;
};

struct argument
{
    const char *name;           /* the name of the argument */
    unsigned char dir;          /* 1 = in, 2 = out */
    unsigned char related_var;  /* index of the related variable */
};

#define EVENTED 1 << 7
struct state_var
{
    const char *name;
    unsigned char itype;        /* MSB: sendEvent flag, 7 LSB: index in upnptypes */
    unsigned char iallowedlist; /* index in allowed values list */
};

static const char *const upnptypes[] = {
    "string",                   // 0
    "ui4",                      // 1
    "i4",                       // 2
    "int",                      // 3
};

static const char *supported_mime_types[] = {
    "/audio/", "adpcm", "basic", "L16", "midi", "mp4", "mp4a-latm", "mpeg",
    "ogg", "s3m", "silk", "vnd.dece.audio", "vnd.digital-winds", "vnd.dra",
    "vnd.dts", "vnd.dts.hd", "vnd.lucent.voice", "vnd.ms-playready.media.pya",
    "vnd.rip", "webm", "x-aac", "x-aiff", "x-caf", "x-dsd", "x-flac",
    "x-matroska", "x-mpegurl", "x-ms-wax", "x-ms-wma", "x-pn-realaudio",
    "x-pn-realaudio-plugin", "x-wav", "xm", "/image/", "bmp", "cgm", "g3fax",
    "gif", "ief", "jp2", "jpeg", "ktx", "pict", "png", "prs.btif", "sgi",
    "svg+xml", "tiff", "vnd.adobe.photoshop", "vnd.dece.graphic", "vnd.djvu",
    "vnd.dvb.subtitle", "vnd.dwg", "vnd.dxf", "vnd.fastbidsheet", "vnd.fpx",
    "vnd.fst", "vnd.fujixerox.edmics-mmr", "vnd.fujixerox.edmics-rlc",
    "vnd.ms-modi", "vnd.ms-photo", "vnd.net-fpx", "vnd.wap.wbmp", "vnd.xiff",
    "webp", "x-3ds", "x-cmu-raster", "x-cmx", "x-freehand", "x-icon",
    "x-macpaint", "x-mrsid-image", "x-pcx", "x-pict", "x-portable-anymap",
    "x-portable-bitmap", "x-portable-graymap", "x-portable-pixmap",
    "x-quicktime", "x-rgb", "x-tga", "x-xbitmap", "x-xpixmap",
    "x-xwindowdump", "/text/", "srt", "/video/", "3gpp", "3gpp2", "h261",
    "h263", "h264", "jpeg", "jpm", "mj2", "mp2t", "mp4", "mpeg", "ogg",
    "quicktime", "vnd.dece.hd", "vnd.dece.mobile", "vnd.dece.pd",
    "vnd.dece.sd", "vnd.dece.video", "vnd.dvb.file", "vnd.fvt", "vnd.mpegurl",
    "vnd.ms-playready.media.pyv", "vnd.uvvu.mp4", "vnd.vivo", "webm", "x-dv",
    "x-f4v", "x-fli", "x-flv", "x-m4v", "x-matroska", "x-mng", "x-ms-asf",
    "x-ms-vob", "x-ms-wm", "x-ms-wmv", "x-ms-wmx", "x-ms-wvx", "x-msvideo",
    "x-sgi-movie", "x-smv", 0
};

static const char *const upnpallowedvalues[] = {
    0,                          /* 0 */
    "OK",                       /* 1 */
    "ContentFormatMismatch",
    "InsufficientBandwidth",
    "UnreliableChannel",
    "Unknown",
    0,
    "Input",                    /* 7 */
    "Output",
    0,
    "BrowseMetadata",           /* 10 */
    "BrowseDirectChildren",
    0,
};

static const char xmlver[] = "<?xml version=\"1.0\"?>\n";
static const char root_service[] = "scpd xmlns=\"urn:schemas-upnp-org:service-1-0\"";

/* For ConnectionManager */
static const struct argument get_protocol_info_args[] = {
    { "Source", 2, 0 },         // SourceProtocolInfo
    { "Sink", 2, 1 },           // SinkProtocolInfo
    { NULL, 0, 0 }
};

static const struct argument get_current_connection_i_ds_args[] = {
    { "ConnectionIDs", 2, 2 },  // CurrentConnectionIDs
    { NULL, 0, 0 }
};

static const struct argument get_current_connection_info_args[] = {
    { "ConnectionID", 1, 7 },   // A_ARG_TYPE_ConnectionID
    { "RcsID", 2, 9 },          // A_ARG_TYPE_RcsID
    { "AVTransportID", 2, 8 },  // A_ARG_TYPE_AVTransportID
    { "ProtocolInfo", 2, 6 },   // A_ARG_TYPE_ProtocolInfo
    { "PeerConnectionManager", 2, 4 },  // A_ARG_TYPE_ConnectionManager
    { "PeerConnectionID", 2, 7 },   // A_ARG_TYPE_ConnectionID
    { "Direction", 2, 5 },      // A_ARG_TYPE_Direction
    { "Status", 2, 3 },         // A_ARG_TYPE_ConnectionStatus
    { NULL, 0, 0 }
};

static const struct action connection_manager_actions[] = {
    { "GetProtocolInfo", get_protocol_info_args },  /* R */
    { "GetCurrentConnectionIDs", get_current_connection_i_ds_args },    /* R */
    { "GetCurrentConnectionInfo", get_current_connection_info_args },   /* R */
    { NULL, 0 }
};

static const struct state_var connection_manager_vars[] = {
    { "SourceProtocolInfo", EVENTED, 0 },   /* required */
    { "SinkProtocolInfo", EVENTED, 0 }, /* required */
    { "CurrentConnectionIDs", EVENTED, 0 }, /* required */
    { "A_ARG_TYPE_ConnectionStatus", 0, 1 },    /* required */
    { "A_ARG_TYPE_ConnectionManager", 0, 0 },   /* required */
    { "A_ARG_TYPE_Direction", 0, 7 },   /* required */
    { "A_ARG_TYPE_ProtocolInfo", 0, 0 },    /* required */
    { "A_ARG_TYPE_ConnectionID", 2, 0 },    /* required */
    { "A_ARG_TYPE_AVTransportID", 2, 0 },   /* required */
    { "A_ARG_TYPE_RcsID", 2, 0 },   /* required */
    { NULL, 0, 0 }
};

static const struct argument get_search_capabilities_args[] = {
    { "SearchCaps", 2, 7 },     // SearchCapabilities
    { NULL, 0, 0 }
};

static const struct argument get_sort_capabilities_args[] = {
    { "SortCaps", 2, 8 },       // SortCapabilities
    { NULL, 0, 0 }
};

static const struct argument get_system_update_id_args[] = {
    { "Id", 2, 9 },             // SystemUpdateID
    { NULL, 0, 0 }
};

static const struct argument browse_args[] = {
    { "ObjectID", 1, 1 },       // A_ARG_TYPE_ObjectID
    { "BrowseFlag", 1, 3 },     // A_ARG_TYPE_BrowseFlag
    { "Filter", 1, 10 },        // A_ARG_TYPE_Filter
    { "StartingIndex", 1, 4 },  // A_ARG_TYPE_Index
    { "RequestedCount", 1, 5 }, // A_ARG_TYPE_Count
    { "SortCriteria", 1, 11 },  // A_ARG_TYPE_SortCriteria
    { "Result", 2, 2 },         // A_ARG_TYPE_Result
    { "NumberReturned", 2, 5 }, // A_ARG_TYPE_Count
    { "TotalMatches", 2, 5 },   // A_ARG_TYPE_Count
    { "UpdateID", 2, 6 },       // A_ARG_TYPE_UpdateID
    { NULL, 0, 0 }
};

static const struct action content_directory_actions[] = {
    { "GetSearchCapabilities", get_search_capabilities_args },  /* R */
    { "GetSortCapabilities", get_sort_capabilities_args },  /* R */
    { "GetSystemUpdateID", get_system_update_id_args }, /* R */
    { "Browse", browse_args },  /* R */
    { NULL, 0 }
};

static const struct state_var content_directory_vars[] = {
    { "TransferIDs", EVENTED, 0 },
    { "A_ARG_TYPE_ObjectID", 0, 0 },
    { "A_ARG_TYPE_Result", 0, 0 },
    { "A_ARG_TYPE_BrowseFlag", 0, 10 },
    { "A_ARG_TYPE_Index", 1, 0 },
    { "A_ARG_TYPE_Count", 1, 0 },
    { "A_ARG_TYPE_UpdateID", 1, 0 },
    { "SearchCapabilities", 0, 0 },
    { "SortCapabilities", 0, 0 },
    { "SystemUpdateID", 1 | EVENTED, 0 },
    { "A_ARG_TYPE_Filter", 0, 0 },
    { "A_ARG_TYPE_SortCriteria", 0, 0 },
    { NULL, 0, 0 }
};

static const struct argument get_is_authorized_args[] = {
    { "DeviceID", 1, 0 },       // A_ARG_TYPE_DeviceID
    { "Result", 2, 1 },         // A_ARG_TYPE_Result
    { NULL, 0, 0 }
};

static const struct argument get_is_validated_args[] = {
    { "DeviceID", 1, 0 },       // A_ARG_TYPE_DeviceID
    { "Result", 2, 1 },         // A_ARG_TYPE_Result
    { NULL, 0, 0 }
};

static const struct action x_ms_media_receiver_registrar_actions[] = {
    { "IsAuthorized", get_is_authorized_args }, /* R */
    { "IsValidated", get_is_validated_args },   /* R */
    { NULL, 0 }
};

static const struct state_var x_ms_media_receiver_registrar_vars[] = {
    { "A_ARG_TYPE_DeviceID", 0, 0 },
    { "A_ARG_TYPE_Result", 3, 0 },
    { NULL, 0, 0 }
};

/* iterative subroutine using a small stack
 * This way, the progam stack usage is kept low */

struct stack_type
{
    int pos;
    int children;
};

static void gen_xml(struct stream *st, const struct xml_elt *elements)
{
    struct stack_type stack[16];    /* stack */
    int top = -1;
    int i = 0;                  /* current node */

    while (elements[i].name != NULL)
    {
        if (top > -1)
            stack[top].children--;

        if (elements[i].name[0] == '/' || elements[i].name[0] == '@')
        {
            int l = 0;
            while (elements[i].name[l] > ' ')
                l++;

            CHUNK_PRINT_ALL(st, "<", elements[i].name + 1, ">");

            if (elements[i].name[0] == '@')
                elements[i].callback(st);
            else
                chunk_print(st, elements[i].value);

            chunk_print(st, "</");
            chunk_print_len(st, elements[i].name + 1, l - 1);
            chunk_print(st, ">");
        }
        else
        {
            CHUNK_PRINT_ALL(st, "<", elements[i].name, ">");
            top++;
            stack[top].pos = i;
            stack[top].children = elements[i].children;
        }

        while (top > -1 && stack[top].children == 0)
        {
            int j = stack[top].pos;

            int l = 0;
            while (elements[j].name[l] > ' ')
                l++;

            chunk_print(st, "</");
            chunk_print_len(st, elements[j].name, l);
            chunk_print(st, ">");

            top--;
        }
        i++;
    }
}

void gen_root_desc(struct stream *st)
{
    const struct xml_elt root_desc[] = {
        { .name = "root xmlns=\"urn:schemas-upnp-org:device-1-0\"", .children = 2 },
        { .name = "specVersion", .children = 2 },
        { .name = "/major", .value = "1" },
        { .name = "/minor", .value = "0" },
        { .name = "device", .children = 14 },
        { .name = "/deviceType", .value = "urn:schemas-upnp-org:device:MediaServer:1" },
        { .name = "/friendlyName", .value = friendly_name },
        { .name = "/manufacturer", .value = ROOTDEV_MANUFACTURER },
        { .name = "/manufacturerURL", .value = ROOTDEV_MANUFACTURERURL },
        { .name = "/modelDescription", .value = ROOTDEV_MODELDESCRIPTION },
        { .name = "/modelName", .value = ROOTDEV_MODELNAME },
        { .name = "/modelNumber", .value = get_microdlna_version() },
        { .name = "/modelURL", .value = ROOTDEV_MANUFACTURERURL },
        { .name = "/serialNumber", .value = ROOTDEV_SERIALNUMBER },
        { .name = "/UDN", .value = uuidvalue },
        { .name = "/dlna:X_DLNADOC xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\"",
          .value = "DMS-1.50" },
        { .name = "/presentationURL", .value = "/" },
        { .name = "iconList", .children = 4 },
        { .name = "icon", .children = 5 },
        { .name = "/mimetype", .value = "image/png" },
        { .name = "/width", .value = "48" },
        { .name = "/height", .value = "48" },
        { .name = "/depth", .value = "24" },
        { .name = "/url", .value = "/icons/sm.png" },
        { .name = "icon", .children = 5 },
        { .name = "/mimetype", .value = "image/png" },
        { .name = "/width", .value = "120" },
        { .name = "/height", .value = "120" },
        { .name = "/depth", .value = "24" },
        { .name = "/url", .value = "/icons/lrg.png" },
        { .name = "icon", .children = 5 },
        { .name = "/mimetype", .value = "image/jpeg" },
        { .name = "/width", .value = "48" },
        { .name = "/height", .value = "48" },
        { .name = "/depth", .value = "24" },
        { .name = "/url", .value = "/icons/sm.jpg" },
        { .name = "icon", .children = 5 },
        { .name = "/mimetype", .value = "image/jpeg" },
        { .name = "/width", .value = "120" },
        { .name = "/height", .value = "120" },
        { .name = "/depth", .value = "24" },
        { .name = "/url", .value = "/icons/lrg.jpg" },
        { .name = "serviceList", .children = 3 },
        { .name = "service", .children = 5 },
        { .name = "/serviceType", .value =
              "urn:schemas-upnp-org:service:ContentDirectory:1" },
        { .name = "/serviceId", .value = "urn:upnp-org:serviceId:ContentDirectory" },
        { .name = "/controlURL", .value = CONTENTDIRECTORY_CONTROLURL },
        { .name = "/eventSubURL", .value = CONTENTDIRECTORY_EVENTURL },
        { .name = "/SCPDURL", .value = CONTENTDIRECTORY_PATH },
        { .name = "service", .children = 5 },
        { .name = "/serviceType", .value =
              "urn:schemas-upnp-org:service:ConnectionManager:1" },
        { .name = "/serviceId", .value = "urn:upnp-org:serviceId:ConnectionManager" },
        { .name = "/controlURL", .value = CONNECTIONMGR_CONTROLURL },
        { .name = "/eventSubURL", .value = CONNECTIONMGR_EVENTURL },
        { .name = "/SCPDURL", .value = CONNECTIONMGR_PATH },
        { .name = "service", .children = 5 },
        { .name = "/serviceType", .value =
              "urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:1" },
        { .name = "/serviceId", .value =
              "urn:microsoft.com:serviceId:X_MS_MediaReceiverRegistrar" },
        { .name = "/controlURL", .value = X_MS_MEDIARECEIVERREGISTRAR_CONTROLURL },
        { .name = "/eventSubURL", .value = X_MS_MEDIARECEIVERREGISTRAR_EVENTURL },
        { .name = "/SCPDURL", .value = X_MS_MEDIARECEIVERREGISTRAR_PATH },
        { NULL, NULL },
    };

    gen_xml(st, root_desc);
}

/* genServiceDesc() :
 * Generate service description with allowed methods and
 * related variables. */
static void gen_service_desc(struct stream *st, const struct action *acts,
                             const struct state_var *vars)
{
    CHUNK_PRINT_ALL(st, xmlver, "<", root_service,
                    "><specVersion><major>1</major><minor>0</minor></specVersion><actionList>");

    for (int i = 0; acts[i].name; i++)
    {
        CHUNK_PRINT_ALL(st, "<action><name>", acts[i].name, "</name>");

        /* argument List */
        const struct argument *args = acts[i].args;
        if (args)
        {
            chunk_print(st, "<argumentList>");

            for (int j = 0; args[j].dir; j++)
            {
                const char *p = vars[args[j].related_var].name;

                CHUNK_PRINT_ALL(st, "<argument><name>",
                                args[j].name ? args[j].name : p,
                                "</name><direction>",
                                args[j].dir == 1 ? "in" : "out",
                                "</direction><relatedStateVariable>",
                                p, "</relatedStateVariable></argument>");
            }

            chunk_print(st, "</argumentList>");
        }
        chunk_print(st, "</action>");
    }
    chunk_print(st, "</actionList><serviceStateTable>");

    for (int i = 0; vars[i].name; i++)
    {
        CHUNK_PRINT_ALL(st, "<stateVariable sendEvents=\"",
                        vars[i].itype & EVENTED ? "yes" : "no",
                        "\"><name>",
                        vars[i].name,
                        "</name><dataType>",
                        upnptypes[vars[i].itype & 0x0f], "</dataType>");

        if (vars[i].iallowedlist)
        {
            chunk_print(st, "<allowedValueList>");
            for (int j = vars[i].iallowedlist; upnpallowedvalues[j]; j++)
            {
                CHUNK_PRINT_ALL(st, "<allowedValue>",
                                upnpallowedvalues[j], "</allowedValue>");
            }
            chunk_print(st, "</allowedValueList>");
        }
        chunk_print(st, "</stateVariable>");
    }

    chunk_print(st, "</serviceStateTable></scpd>");
}

/* sendContentDirectory() :
 * Generate the ContentDirectory xml description */
void send_content_directory(struct stream *st)
{
    gen_service_desc(st, content_directory_actions, content_directory_vars);
}

/* sendConnectionManager() :
 * Generate the ConnectionManager xml description */
void send_connection_manager(struct stream *st)
{
    gen_service_desc(st, connection_manager_actions, connection_manager_vars);
}

/* sendX_MS_MediaReceiverRegistrar() :
 * Generate the X_MS_MediaReceiverRegistrar xml description */
void send_x_ms_media_receiver_registrar(struct stream *st)
{
    gen_service_desc(st, x_ms_media_receiver_registrar_actions,
                     x_ms_media_receiver_registrar_vars);
}

void get_vars_content_directory(struct stream *fh)
{
    const struct xml_elt data[] = {
        { .name = "e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\" "
                  "xmlns:s=\"urn:schemas-upnp-org:service:ContentDirectory:1\"", .children = 2 },
        { .name = "e:property", .children = 1 },
        { .name = "TransferIDs", .children = 0 },
        { .name = "e:property", .children = 1 },
        { .name = "/SystemUpdateID", .value = "0" },
        { NULL, NULL },
    };

    gen_xml(fh, data);
}

void get_resource_protocol_info_values(struct stream *fh)
{
    const char *main_type = &supported_mime_types[0][1];

    CHUNK_PRINT_ALL(fh, "http-get:*:", main_type, supported_mime_types[1], ":*");

    for (int i = 2; supported_mime_types[i]; i++)
    {
        if (supported_mime_types[i][0] == '/')
            main_type = &supported_mime_types[i][1];
        else
            CHUNK_PRINT_ALL(fh, ",http-get:*:", main_type, supported_mime_types[i], ":*");
    }
}

void get_vars_connection_manager(struct stream *fh)
{
    const struct xml_elt data[] = {
        { .name =
              "e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\" "
              "xmlns:s=\"urn:schemas-upnp-org:service:ConnectionManager:1\"", .children = 3 },
        { .name = "e:property", .children = 1 },
        { .name = "@SourceProtocolInfo", .callback = get_resource_protocol_info_values },
        { .name = "e:property", .children = 1 },
        { .name = "SinkProtocolInfo", .children = 0 },
        { .name = "e:property", .children = 1 },
        { .name = "/CurrentConnectionIDs", .value = "0" },
        { NULL, NULL },
    };

    gen_xml(fh, data);
}
