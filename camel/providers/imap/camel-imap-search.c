/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-search.c: IMAP folder search */

/*
 *  Authors:
 *    Dan Winship <danw@ximian.com>
 *
 *  Copyright 2000, 2001 Ximian, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "camel-imap-command.h"
#include "camel-imap-folder.h"
#include "camel-imap-store.h"
#include "camel-imap-search.h"
#include "camel-imap-private.h"

static ESExpResult *
imap_body_contains (struct _ESExp *f, int argc, struct _ESExpResult **argv,
		    CamelFolderSearch *s);

static void
camel_imap_search_class_init (CamelImapSearchClass *camel_imap_search_class)
{
	/* virtual method overload */
	CamelFolderSearchClass *camel_folder_search_class =
		CAMEL_FOLDER_SEARCH_CLASS (camel_imap_search_class);
	
	/* virtual method overload */
	camel_folder_search_class->body_contains = imap_body_contains;
}

CamelType
camel_imap_search_get_type (void)
{
	static CamelType camel_imap_search_type = CAMEL_INVALID_TYPE;
	
	if (camel_imap_search_type == CAMEL_INVALID_TYPE) {
		camel_imap_search_type = camel_type_register (
			CAMEL_FOLDER_SEARCH_TYPE, "CamelImapSearch",
			sizeof (CamelImapSearch),
			sizeof (CamelImapSearchClass),
			(CamelObjectClassInitFunc) camel_imap_search_class_init,
			NULL, NULL, NULL);
	}

	return camel_imap_search_type;
}

static ESExpResult *
imap_body_contains (struct _ESExp *f, int argc, struct _ESExpResult **argv,
		    CamelFolderSearch *s)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (s->folder->parent_store);
	char *value = argv[0]->value.string;
	CamelImapResponse *response = NULL;
	char *result, *p, *lasts = NULL, *real_uid;
	const char *uid = "";
	ESExpResult *r;
	CamelMessageInfo *info;
	GHashTable *uid_hash = NULL;

	if (s->current) {
		uid = camel_message_info_uid (s->current);
		r = e_sexp_result_new (f, ESEXP_RES_BOOL);
		r->value.bool = FALSE;
		response = camel_imap_command (store, s->folder, NULL,
					       "UID SEARCH UID %s BODY \"%s\"",
					       uid, value);
	} else {
		r = e_sexp_result_new (f, ESEXP_RES_ARRAY_PTR);
		
		if (argc == 1 && *value == '\0' && s->folder) {
			/* optimise the match "" case - match everything */
			int i;
			
			r->value.ptrarray = g_ptr_array_new ();
			for (i = 0; i < s->summary->len; i++) {
				CamelMessageInfo *info = g_ptr_array_index (s->summary, i);
				g_ptr_array_add (r->value.ptrarray, (char *)camel_message_info_uid (info));
			}
		} else {
			/* FIXME: danw: what if we have multiple string args? */
			r->value.ptrarray = g_ptr_array_new ();
			response = camel_imap_command (store, s->folder, NULL,
						       "UID SEARCH BODY \"%s\"",
						       value);
		}
	}
	
	if (!response)
		return r;
	result = camel_imap_response_extract (store, response, "SEARCH", NULL);
	if (!result)
		return r;
	
	p = result + sizeof ("* SEARCH");
	for (p = strtok_r (p, " ", &lasts); p; p = strtok_r (NULL, " ", &lasts)) {
		if (s->current) {
			if (!strcmp (uid, p)) {
				r->value.bool = TRUE;
				break;
			}
		} else {
			/* if we need to setup a hash of summary items, this way we get
			   access to the summary memory which is locked for the duration of
			   the search, and wont vanish on us */
			if (uid_hash == NULL) {
				int i;
				
				uid_hash = g_hash_table_new (g_str_hash, g_str_equal);
				for (i = 0; i < s->summary->len; i++) {
					info = s->summary->pdata[i];
					g_hash_table_insert (uid_hash, (char *)camel_message_info_uid (info), info);
				}
			}
			if (g_hash_table_lookup_extended (uid_hash, p, (void *)&real_uid, (void *)&info))
				g_ptr_array_add (r->value.ptrarray, real_uid);
		}
	}
	
	/* we could probably cache this globally, but its probably not worth it */
	if (uid_hash)
		g_hash_table_destroy (uid_hash);
	
	return r;
}

/**
 * camel_imap_search_new:
 *
 * Return value: A new CamelImapSearch widget.
 **/
CamelFolderSearch *
camel_imap_search_new (void)
{
	CamelFolderSearch *new = CAMEL_FOLDER_SEARCH (camel_object_new (camel_imap_search_get_type ()));

	camel_folder_search_construct (new);
	return new;
}
