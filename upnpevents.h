#pragma once
/* MicroDLNA project
 * http://microdlna.sourceforge.net/
 *
 * MicroDLNA media server
 * Copyright (c) 2016, Gabor Simon
 * All rights reserved.
 *
 * Based on the MiniDLNA project:
 * Copyright (C) 2008-2009  Justin Maggard
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
 *
 * Portions of the code from the MiniUPnP project:
 *
 * Copyright (c) 2006-2007, Thomas Bernard
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

struct upnphttp;

void clear_upnpevent_subscribers(void);

void upnpevents_selectfds(fd_set *readset, fd_set *writeset, int *max_fd);

void upnpevents_processfds(fd_set *readset, fd_set *writeset);

void upnpevents_removed_timedout_subs(void);

void upnpevents_clear_notify_list(void);

void process_http_subscribe_upnphttp(struct upnphttp *h);

void process_http_un_subscribe_upnphttp(struct upnphttp *h);
