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
 */

struct upnphttp;

void browse_content_directory(struct upnphttp *);

void unsupported_soap_action(struct upnphttp *);

void get_search_capabilities(struct upnphttp *);

void get_sort_capabilities(struct upnphttp *);

void get_protocol_info(struct upnphttp *);

void invalid_soap_action(struct upnphttp *);
