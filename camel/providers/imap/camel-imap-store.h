/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-store.h : class for an imap store */

/* 
 * Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 2000 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */


#ifndef CAMEL_IMAP_STORE_H
#define CAMEL_IMAP_STORE_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include "camel-imap-types.h"
#include "camel-disco-store.h"

#ifdef ENABLE_THREADS
#include "e-util/e-msgport.h"

typedef struct _CamelImapMsg CamelImapMsg;

struct _CamelImapMsg {
	EMsg msg;

	void (*receive)(CamelImapStore *store, struct _CamelImapMsg *m);
	void (*free)(CamelImapStore *store, struct _CamelImapMsg *m);
};

CamelImapMsg *camel_imap_msg_new(void (*receive)(CamelImapStore *store, struct _CamelImapMsg *m),
				 void (*free)(CamelImapStore *store, struct _CamelImapMsg *m),
				 size_t size);
void camel_imap_msg_queue(CamelImapStore *store, CamelImapMsg *msg);

#endif

#define CAMEL_IMAP_STORE_TYPE     (camel_imap_store_get_type ())
#define CAMEL_IMAP_STORE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_IMAP_STORE_TYPE, CamelImapStore))
#define CAMEL_IMAP_STORE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_IMAP_STORE_TYPE, CamelImapStoreClass))
#define CAMEL_IS_IMAP_STORE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_IMAP_STORE_TYPE))

typedef enum {
	IMAP_LEVEL_UNKNOWN,
	IMAP_LEVEL_IMAP4,
	IMAP_LEVEL_IMAP4REV1
} CamelImapServerLevel;

#define IMAP_CAPABILITY_IMAP4			(1 << 0)
#define IMAP_CAPABILITY_IMAP4REV1		(1 << 1)
#define IMAP_CAPABILITY_STATUS			(1 << 2)
#define IMAP_CAPABILITY_NAMESPACE		(1 << 3)
#define IMAP_CAPABILITY_UIDPLUS			(1 << 4)
#define IMAP_CAPABILITY_LITERALPLUS		(1 << 5)
#define IMAP_CAPABILITY_useful_lsub		(1 << 6)

#define IMAP_PARAM_OVERRIDE_NAMESPACE		(1 << 0)
#define IMAP_PARAM_CHECK_ALL			(1 << 1)
#define IMAP_PARAM_FILTER_INBOX			(1 << 2)

struct _CamelImapStore {
	CamelDiscoStore parent_object;	
	struct _CamelImapStorePrivate *priv;
	
	/* Information about the command channel / connection status */
	gboolean connected;
	char tag_prefix;
	guint32 command;
	CamelFolder *current_folder;
	
	/* Information about the server */
	CamelImapServerLevel server_level;
	guint32 capabilities, parameters;
	char *namespace, dir_sep, *base_url, *storage_path;
	GHashTable *authtypes, *subscribed_folders;
#ifdef ENABLE_THREADS
	EThread *async_thread;
#endif
};


typedef struct {
	CamelDiscoStoreClass parent_class;

} CamelImapStoreClass;


/* Standard Camel function */
CamelType camel_imap_store_get_type (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_IMAP_STORE_H */
