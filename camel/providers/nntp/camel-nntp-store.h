/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-nntp-store.h : class for an nntp store */

/* 
 *
 * Copyright (C) 2000 Ximian, Inc. <toshok@ximian.com>
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


#ifndef CAMEL_NNTP_STORE_H
#define CAMEL_NNTP_STORE_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include "camel-remote-store.h"
#include "camel-nntp-newsrc.h"
#include "camel-nntp-types.h"

#include "config.h"

#ifdef ENABLE_THREADS
#include "e-util/e-msgport.h"
#endif

#define CAMEL_NNTP_STORE_TYPE     (camel_nntp_store_get_type ())
#define CAMEL_NNTP_STORE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_NNTP_STORE_TYPE, CamelNNTPStore))
#define CAMEL_NNTP_STORE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_NNTP_STORE_TYPE, CamelNNTPStoreClass))
#define CAMEL_IS_NNTP_STORE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_NNTP_STORE_TYPE))

enum {
	CAMEL_NNTP_OVER_FROM,
	CAMEL_NNTP_OVER_SUBJECT,
	CAMEL_NNTP_OVER_DATE,
	CAMEL_NNTP_OVER_MESSAGE_ID,
	CAMEL_NNTP_OVER_REFERENCES,
	CAMEL_NNTP_OVER_BYTES,

	CAMEL_NNTP_OVER_LAST
};

struct CamelNNTPOverField {
	int index;
	gboolean full; /* full in the OVER sense - the field name
                          precedes the ':' in the XOVER list. */
};

struct CamelNNTPStore {
	CamelRemoteStore parent_object;	

#define CAMEL_NNTP_EXT_SEARCH     (1<<0)
#define CAMEL_NNTP_EXT_SETGET     (1<<1)
#define CAMEL_NNTP_EXT_OVER       (1<<2)
#define CAMEL_NNTP_EXT_XPATTEXT   (1<<3)
#define CAMEL_NNTP_EXT_XACTIVE    (1<<4)
#define CAMEL_NNTP_EXT_LISTMOTD   (1<<5)
#define CAMEL_NNTP_EXT_LISTSUBSCR (1<<6)
#define CAMEL_NNTP_EXT_LISTPNAMES (1<<7)
	guint32 extensions;

	gboolean posting_allowed;

	int num_overview_fields;
	CamelNNTPOverField overview_field[ CAMEL_NNTP_OVER_LAST ];

	CamelNNTPNewsrc *newsrc;
	CamelNNTPGroupList *group_list;

#ifdef ENABLE_THREADS
	EMutex *command_lock;
#endif
};

struct CamelNNTPStoreClass {
	CamelRemoteStoreClass parent_class;

};

#ifdef ENABLE_THREADS
#define CAMEL_NNTP_STORE_LOCK(f) (e_mutex_lock(((CamelNNTPStore *) f)->command_lock))
#define CAMEL_NNTP_STORE_UNLOCK(f) (e_mutex_unlock(((CamelNNTPStore *) f)->command_lock))
#else
#define CAMEL_NNTP_STORE_LOCK(f)
#define CAMEL_NNTP_STORE_UNLOCK(f)
#endif

/* public methods */
void camel_nntp_store_open (CamelNNTPStore *store, CamelException *ex);
void camel_nntp_store_close (CamelNNTPStore *store, gboolean expunge,
			     CamelException *ex);

void camel_nntp_store_subscribe_group (CamelStore *store, const gchar *group_name);
void camel_nntp_store_unsubscribe_group (CamelStore *store, const gchar *group_name);
GList *camel_nntp_store_list_subscribed_groups(CamelStore *store);

gchar *camel_nntp_store_get_toplevel_dir (CamelNNTPStore *store);

/* support functions */
int camel_nntp_command (CamelNNTPStore *store, CamelException *ex, char **ret, char *fmt, ...);

/* Standard Camel function */
CamelType camel_nntp_store_get_type (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_NNTP_STORE_H */


