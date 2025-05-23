#pragma once
/* MiniUPnP project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 *
 * Copyright (c) 2016, Gabor Simon
 * All rights reserved.
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

/* Paths and other URLs in the microdlna http server */

#define ROOTDESC_PATH                          "/rootDesc.xml"

#define CONTENTDIRECTORY_PATH                   "/ContentDir.xml"
#define CONTENTDIRECTORY_CONTROLURL             "/ctl/ContentDir"
#define CONTENTDIRECTORY_EVENTURL               "/evt/ContentDir"

#define CONNECTIONMGR_PATH                      "/ConnectionMgr.xml"
#define CONNECTIONMGR_CONTROLURL                "/ctl/ConnectionMgr"
#define CONNECTIONMGR_EVENTURL                  "/evt/ConnectionMgr"

#define X_MS_MEDIARECEIVERREGISTRAR_PATH        "/X_MS_MediaReceiverRegistrar.xml"
#define X_MS_MEDIARECEIVERREGISTRAR_CONTROLURL  "/ctl/X_MS_MediaReceiverRegistrar"
#define X_MS_MEDIARECEIVERREGISTRAR_EVENTURL    "/evt/X_MS_MediaReceiverRegistrar"
