/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-command.h: IMAP command sending/parsing routines */

/* 
 * Authors:
 *   Dan Winship <danw@ximian.com>
 *   Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 2000, 2001 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU Lesser General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */


#ifndef CAMEL_IMAP_COMMAND_H
#define CAMEL_IMAP_COMMAND_H 1

#include <glib.h>
#include "camel-imap-types.h"

G_BEGIN_DECLS

typedef enum {
	CAMEL_IMAP_RESPONSE_ERROR,
	CAMEL_IMAP_RESPONSE_CONTINUATION,
	CAMEL_IMAP_RESPONSE_UNTAGGED,
	CAMEL_IMAP_RESPONSE_TAGGED
} CamelImapResponseType;

struct _CamelImapResponse {
	CamelFolder *folder;
	GPtrArray *untagged;
	char *status;
};

CamelImapResponse *camel_imap_command              (CamelImapStore *store,
						    CamelFolder *folder,
						    CamelException *ex,
						    const char *fmt, ...);
CamelImapResponse *camel_imap_command_continuation (CamelImapStore *store,
						    const char *cmd,
						    size_t cmdlen,
						    CamelException *ex);

void  camel_imap_response_free                     (CamelImapStore *store,
						    CamelImapResponse *response);
void  camel_imap_response_free_without_processing  (CamelImapStore *store,
						    CamelImapResponse *response);
char *camel_imap_response_extract                  (CamelImapStore *store,
						    CamelImapResponse *response,
						    const char *type,
						    CamelException *ex);
char *camel_imap_response_extract_continuation     (CamelImapStore *store,
						    CamelImapResponse *response,
						    CamelException *ex);

gboolean           camel_imap_command_start        (CamelImapStore *store,
						    CamelFolder *folder,
						    CamelException *ex,
						    const char *fmt, ...);
CamelImapResponseType camel_imap_command_response  (CamelImapStore *store,
						    char **respbuf,
						    CamelException *ex);

G_END_DECLS

#endif /* CAMEL_IMAP_COMMAND_H */
