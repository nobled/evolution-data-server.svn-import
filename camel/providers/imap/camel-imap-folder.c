/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-folder.c: class for an imap folder */

/* 
 * Authors:
 *   Dan Winship <danw@ximian.com>
 *   Jeffrey Stedfast <fejj@ximian.com> 
 *
 * Copyright (C) 2000, 2001 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * License as published by the Free Software Foundation.
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

#ifdef HAVE_CONFIG_H
#include <config.h> 
#endif

#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>

#include "e-util/e-path.h"

#include "camel-imap-folder.h"
#include "camel-imap-command.h"
#include "camel-imap-message-cache.h"
#include "camel-imap-private.h"
#include "camel-imap-search.h"
#include "camel-imap-store.h"
#include "camel-imap-summary.h"
#include "camel-imap-utils.h"
#include "camel-imap-wrapper.h"
#include "camel-data-wrapper.h"
#include "camel-disco-diary.h"
#include "camel-exception.h"
#include "camel-mime-filter-crlf.h"
#include "camel-mime-filter-from.h"
#include "camel-mime-message.h"
#include "camel-mime-utils.h"
#include "camel-multipart.h"
#include "camel-operation.h"
#include "camel-session.h"
#include "camel-stream-buffer.h"
#include "camel-stream-filter.h"
#include "camel-stream-mem.h"
#include "camel-stream.h"
#include "string-utils.h"

#define UID_SET_LIMIT  (-1)


#define CF_CLASS(o) (CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(o)))
static CamelDiscoFolderClass *disco_folder_class = NULL;

static void imap_finalize (CamelObject *object);
static void imap_rescan (CamelFolder *folder, int exists, CamelException *ex);
static void imap_refresh_info (CamelFolder *folder, CamelException *ex);
static void imap_sync_online (CamelFolder *folder, CamelException *ex);
static void imap_sync_offline (CamelFolder *folder, CamelException *ex);
static void imap_expunge_uids_online (CamelFolder *folder, GPtrArray *uids, CamelException *ex);
static void imap_expunge_uids_offline (CamelFolder *folder, GPtrArray *uids, CamelException *ex);
static void imap_expunge_uids_resyncing (CamelFolder *folder, GPtrArray *uids, CamelException *ex);
static void imap_cache_message (CamelDiscoFolder *disco_folder, const char *uid, CamelException *ex);
static void imap_rename (CamelFolder *folder, const char *new);

/* message manipulation */
static CamelMimeMessage *imap_get_message (CamelFolder *folder, const gchar *uid,
					   CamelException *ex);
static void imap_append_online (CamelFolder *folder, CamelMimeMessage *message,
				 const CamelMessageInfo *info, CamelException *ex);
static void imap_append_offline (CamelFolder *folder, CamelMimeMessage *message,
				  const CamelMessageInfo *info, CamelException *ex);
static void imap_append_resyncing (CamelFolder *folder, CamelMimeMessage *message,
				   const CamelMessageInfo *info, CamelException *ex);

static void imap_copy_online (CamelFolder *source, GPtrArray *uids,
			      CamelFolder *destination, CamelException *ex);
static void imap_copy_offline (CamelFolder *source, GPtrArray *uids,
			       CamelFolder *destination, CamelException *ex);
static void imap_copy_resyncing (CamelFolder *source, GPtrArray *uids,
				 CamelFolder *destination, CamelException *ex);
static void imap_move_messages_to (CamelFolder *source, GPtrArray *uids,
				   CamelFolder *destination, CamelException *ex);

/* searching */
static GPtrArray *imap_search_by_expression (CamelFolder *folder, const char *expression, CamelException *ex);
static GPtrArray *imap_search_by_uids	    (CamelFolder *folder, const char *expression, GPtrArray *uids, CamelException *ex);
static void       imap_search_free          (CamelFolder *folder, GPtrArray *uids);

static void imap_thaw (CamelFolder *folder);

GData *parse_fetch_response (CamelImapFolder *imap_folder, char *msg_att);

static void
camel_imap_folder_class_init (CamelImapFolderClass *camel_imap_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS (camel_imap_folder_class);
	CamelDiscoFolderClass *camel_disco_folder_class = CAMEL_DISCO_FOLDER_CLASS (camel_imap_folder_class);

	disco_folder_class = CAMEL_DISCO_FOLDER_CLASS (camel_type_get_global_classfuncs (camel_disco_folder_get_type ()));

	/* virtual method overload */
	camel_folder_class->get_message = imap_get_message;
	camel_folder_class->move_messages_to = imap_move_messages_to;
	camel_folder_class->rename = imap_rename;
	camel_folder_class->search_by_expression = imap_search_by_expression;
	camel_folder_class->search_by_uids = imap_search_by_uids;
	camel_folder_class->search_free = imap_search_free;
	camel_folder_class->thaw = imap_thaw;

	camel_disco_folder_class->refresh_info_online = imap_refresh_info;
	camel_disco_folder_class->sync_online = imap_sync_online;
	camel_disco_folder_class->sync_offline = imap_sync_offline;
	/* We don't sync flags at resync time: the online code will
	 * deal with it eventually.
	 */
	camel_disco_folder_class->sync_resyncing = imap_sync_offline;
	camel_disco_folder_class->expunge_uids_online = imap_expunge_uids_online;
	camel_disco_folder_class->expunge_uids_offline = imap_expunge_uids_offline;
	camel_disco_folder_class->expunge_uids_resyncing = imap_expunge_uids_resyncing;
	camel_disco_folder_class->append_online = imap_append_online;
	camel_disco_folder_class->append_offline = imap_append_offline;
	camel_disco_folder_class->append_resyncing = imap_append_resyncing;
	camel_disco_folder_class->copy_online = imap_copy_online;
	camel_disco_folder_class->copy_offline = imap_copy_offline;
	camel_disco_folder_class->copy_resyncing = imap_copy_resyncing;
	camel_disco_folder_class->cache_message = imap_cache_message;
}

static void
camel_imap_folder_init (gpointer object, gpointer klass)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (object);
	CamelFolder *folder = CAMEL_FOLDER (object);
	
	folder->folder_flags |= (CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY |
				 CAMEL_FOLDER_HAS_SEARCH_CAPABILITY);
	
	imap_folder->priv = g_malloc0(sizeof(*imap_folder->priv));
#ifdef ENABLE_THREADS
	imap_folder->priv->search_lock = e_mutex_new(E_MUTEX_SIMPLE);
	imap_folder->priv->cache_lock = e_mutex_new(E_MUTEX_REC);
#endif

	imap_folder->need_rescan = TRUE;
}

CamelType
camel_imap_folder_get_type (void)
{
	static CamelType camel_imap_folder_type = CAMEL_INVALID_TYPE;
	
	if (camel_imap_folder_type == CAMEL_INVALID_TYPE) {
		camel_imap_folder_type =
			camel_type_register (CAMEL_DISCO_FOLDER_TYPE, "CamelImapFolder",
					     sizeof (CamelImapFolder),
					     sizeof (CamelImapFolderClass),
					     (CamelObjectClassInitFunc) camel_imap_folder_class_init,
					     NULL,
					     (CamelObjectInitFunc) camel_imap_folder_init,
					     (CamelObjectFinalizeFunc) imap_finalize);
	}
	
	return camel_imap_folder_type;
}

CamelFolder *
camel_imap_folder_new (CamelStore *parent, const char *folder_name,
		       const char *folder_dir, CamelException *ex)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (parent);
	CamelFolder *folder;
	CamelImapFolder *imap_folder;
	const char *short_name;
	char *summary_file;

	if (camel_mkdir_hier (folder_dir, S_IRWXU) != 0) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not create directory %s: %s"),
				      folder_dir, g_strerror (errno));
		return NULL;
	}

	folder = CAMEL_FOLDER (camel_object_new (camel_imap_folder_get_type ()));
	short_name = strrchr (folder_name, imap_store->dir_sep);
	if (short_name)
		short_name++;
	else
		short_name = folder_name;
	camel_folder_construct (folder, parent, folder_name, short_name);

	summary_file = g_strdup_printf ("%s/summary", folder_dir);
	folder->summary = camel_imap_summary_new (summary_file);
	g_free (summary_file);
	if (!folder->summary) {
		camel_object_unref (CAMEL_OBJECT (folder));
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not load summary for %s"),
				      folder_name);
		return NULL;
	}

	imap_folder = CAMEL_IMAP_FOLDER (folder);
	imap_folder->cache = camel_imap_message_cache_new (folder_dir, folder->summary, ex);
	if (!imap_folder->cache) {
		camel_object_unref (CAMEL_OBJECT (folder));
		return NULL;
	}

	if ((imap_store->parameters & IMAP_PARAM_FILTER_INBOX) &&
	    !g_strcasecmp (folder_name, "INBOX"))
		folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;

	imap_folder->search = camel_imap_search_new(folder_dir);

	return folder;
}

/* Called with the store's command_lock locked */
void
camel_imap_folder_selected (CamelFolder *folder, CamelImapResponse *response,
			    CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapSummary *imap_summary = CAMEL_IMAP_SUMMARY (folder->summary);
	unsigned long exists = 0, validity = 0, val, uid;
	CamelMessageInfo *info;
	GData *fetch_data;
	int i, count;
	char *resp;
	
	CAMEL_IMAP_STORE_ASSERT_LOCKED (folder->parent_store, command_lock);
	
	count = camel_folder_summary_count (folder->summary);
	
	for (i = 0; i < response->untagged->len; i++) {
		resp = response->untagged->pdata[i] + 2;
		if (!g_strncasecmp (resp, "FLAGS ", 6) &&
		    !folder->permanent_flags) {
			resp += 6;
			folder->permanent_flags = imap_parse_flag_list (&resp);
		} else if (!g_strncasecmp (resp, "OK [PERMANENTFLAGS ", 19)) {
			resp += 19;
			folder->permanent_flags = imap_parse_flag_list (&resp);
		} else if (!g_strncasecmp (resp, "OK [UIDVALIDITY ", 16)) {
			validity = strtoul (resp + 16, NULL, 10);
		} else if (isdigit ((unsigned char)*resp)) {
			unsigned long num = strtoul (resp, &resp, 10);
			
			if (!g_strncasecmp (resp, " EXISTS", 7)) {
				exists = num;
				/* Remove from the response so nothing
				 * else tries to interpret it.
				 */
				g_free (response->untagged->pdata[i]);
				g_ptr_array_remove_index (response->untagged, i--);
			}
		}
	}
	
	if (camel_disco_store_status (CAMEL_DISCO_STORE (folder->parent_store)) == CAMEL_DISCO_STORE_RESYNCING) {
		if (validity != imap_summary->validity) {
			camel_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_SUMMARY_INVALID,
					      _("Folder was destroyed and recreated on server."));
			return;
		}
		
		/* FIXME: find missing UIDs ? */
		return;
	}
	
	if (!imap_summary->validity)
		imap_summary->validity = validity;
	else if (validity != imap_summary->validity) {
		imap_summary->validity = validity;
		camel_folder_summary_clear (folder->summary);
		CAMEL_IMAP_FOLDER_LOCK (imap_folder, cache_lock);
		camel_imap_message_cache_clear (imap_folder->cache);
		CAMEL_IMAP_FOLDER_UNLOCK (imap_folder, cache_lock);
		imap_folder->need_rescan = FALSE;
		camel_imap_folder_changed (folder, exists, NULL, ex);
		return;
	}
	
	/* If we've lost messages, we have to rescan everything */
	if (exists < count)
		imap_folder->need_rescan = TRUE;
	else if (count != 0 && !imap_folder->need_rescan) {
		CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
		
		/* Similarly, if the UID of the highest message we
		 * know about has changed, then that indicates that
		 * messages have been both added and removed, so we
		 * have to rescan to find the removed ones. (We pass
		 * NULL for the folder since we know that this folder
		 * is selected, and we don't want camel_imap_command
		 * to worry about it.)
		 */
		response = camel_imap_command (store, NULL, ex, "FETCH %d UID", count);
		if (!response)
			return;
		uid = 0;
		for (i = 0; i < response->untagged->len; i++) {
			resp = response->untagged->pdata[i];
			val = strtoul (resp + 2, &resp, 10);
			if (val == 0)
				continue;
			if (!g_strcasecmp (resp, " EXISTS")) {
				/* Another one?? */
				exists = val;
				continue;
			}
			if (uid != 0 || val != count || g_strncasecmp (resp, " FETCH (", 8) != 0)
				continue;
			
			fetch_data = parse_fetch_response (imap_folder, resp + 7);
			uid = strtoul (g_datalist_get_data (&fetch_data, "UID"), NULL, 10);
			g_datalist_clear (&fetch_data);
		}
		camel_imap_response_free_without_processing (store, response);
		
		info = camel_folder_summary_index (folder->summary, count - 1);
		val = strtoul (camel_message_info_uid (info), NULL, 10);
		camel_folder_summary_info_free (folder->summary, info);
		if (uid == 0 || uid != val)
			imap_folder->need_rescan = TRUE;
	}
	
	/* Now rescan if we need to */
	if (imap_folder->need_rescan) {
		imap_rescan (folder, exists, ex);
		return;
	}
	
	/* If we don't need to rescan completely, but new messages
	 * have been added, find out about them.
	 */
	if (exists > count)
		camel_imap_folder_changed (folder, exists, NULL, ex);
	
	/* And we're done. */
}	

static void           
imap_finalize (CamelObject *object)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (object);

	if (imap_folder->search)
		camel_object_unref (CAMEL_OBJECT (imap_folder->search));
	if (imap_folder->cache)
		camel_object_unref (CAMEL_OBJECT (imap_folder->cache));

#ifdef ENABLE_THREADS
	e_mutex_destroy(imap_folder->priv->search_lock);
	e_mutex_destroy(imap_folder->priv->cache_lock);
#endif
	g_free(imap_folder->priv);
}

static void
imap_rename (CamelFolder *folder, const char *new)
{
	CamelImapFolder *imap_folder = (CamelImapFolder *)folder;
	CamelImapStore *imap_store = (CamelImapStore *)folder->parent_store;
	char *folder_dir, *summary_path;

	folder_dir = e_path_to_physical (imap_store->storage_path, new);
	summary_path = g_strdup_printf("%s/summary", folder_dir);

	CAMEL_IMAP_FOLDER_LOCK (folder, cache_lock);
	camel_imap_message_cache_set_path(imap_folder->cache, folder_dir);
	CAMEL_IMAP_FOLDER_UNLOCK (folder, cache_lock);

	camel_folder_summary_set_filename(folder->summary, summary_path);

	g_free(summary_path);
	g_free(folder_dir);

	((CamelFolderClass *)disco_folder_class)->rename(folder, new);
}

static void
imap_refresh_info (CamelFolder *folder, CamelException *ex)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapResponse *response;

	if (camel_disco_store_status (CAMEL_DISCO_STORE (imap_store)) == CAMEL_DISCO_STORE_OFFLINE)
		return;

	if (camel_folder_is_frozen (folder)) {
		imap_folder->need_refresh = TRUE;
		return;
	}

	/* If the folder isn't selected, select it (which will force
	 * a rescan if one is needed.
	 */
	if (imap_store->current_folder != folder) {
		response = camel_imap_command (imap_store, folder, ex, NULL);
		camel_imap_response_free (imap_store, response);
		return;
	}

	/* Otherwise, if we need a rescan, do it, and if not, just do
	 * a NOOP to give the server a chance to tell us about new
	 * messages.
	 */
	if (imap_folder->need_rescan)
		imap_rescan (folder, camel_folder_summary_count (folder->summary), ex);
	else {
		response = camel_imap_command (imap_store, folder, ex, "NOOP");
		camel_imap_response_free (imap_store, response);
	}
}

/* Called with the store's command_lock locked */
static void
imap_rescan (CamelFolder *folder, int exists, CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	struct {
		char *uid;
		guint32 flags;
	} *new;
	char *resp;
	CamelImapResponseType type;
	int i, seq, summary_len, summary_got;
	CamelMessageInfo *info;
	CamelImapMessageInfo *iinfo;
	GArray *removed;
	gboolean ok;
	CamelFolderChangeInfo *changes = NULL;

	CAMEL_IMAP_STORE_ASSERT_LOCKED (store, command_lock);
	imap_folder->need_rescan = FALSE;
	
	summary_len = camel_folder_summary_count (folder->summary);
	if (summary_len == 0) {
		if (exists)
			camel_imap_folder_changed (folder, exists, NULL, ex);
		return;
	}
	
	/* Check UIDs and flags of all messages we already know of. */
	camel_operation_start (NULL, _("Scanning for changed messages"));
	info = camel_folder_summary_index (folder->summary, summary_len - 1);
	ok = camel_imap_command_start (store, folder, ex,
				       "UID FETCH 1:%s (FLAGS)",
				       camel_message_info_uid (info));
	camel_folder_summary_info_free (folder->summary, info);
	if (!ok) {
		camel_operation_end (NULL);
		return;
	}
	
	new = g_malloc0 (summary_len * sizeof (*new));
	summary_got = 0;
	while ((type = camel_imap_command_response (store, &resp, ex)) == CAMEL_IMAP_RESPONSE_UNTAGGED) {
		GData *data;
		char *uid;
		guint32 flags;
		
		data = parse_fetch_response (imap_folder, resp);
		g_free (resp);
		if (!data)
			continue;
		
		seq = GPOINTER_TO_INT (g_datalist_get_data (&data, "SEQUENCE"));
		uid = g_datalist_get_data (&data, "UID");
		flags = GPOINTER_TO_UINT (g_datalist_get_data (&data, "FLAGS"));
		
		if (!uid || !seq || seq >= summary_len) {
			g_datalist_clear (&data);
			continue;
		}
		
		camel_operation_progress (NULL, ++summary_got * 100 / summary_len);
		new[seq - 1].uid = g_strdup (uid);
		new[seq - 1].flags = flags;
		g_datalist_clear (&data);
	}
	
	camel_operation_end (NULL);
	if (type == CAMEL_IMAP_RESPONSE_ERROR) {
		for (i = 0; i < summary_len && new[i].uid; i++)
			g_free (new[i].uid);
		g_free (new);
		return;
	}
	/* Free the final tagged response */
	g_free (resp);
	
	/* If we find a UID in the summary that doesn't correspond to
	 * the UID in the folder, then either: (a) it's a real UID,
	 * but the message was deleted on the server, or (b) it's a
	 * fake UID, and needs to be removed from the summary in order
	 * to sync up with the server. So either way, we remove it
	 * from the summary.
	 */
	removed = g_array_new (FALSE, FALSE, sizeof (int));
	for (i = 0; i < summary_len && new[i].uid; i++) {
		info = camel_folder_summary_index (folder->summary, i);
		iinfo = (CamelImapMessageInfo *)info;
		
		if (strcmp (camel_message_info_uid (info), new[i].uid) != 0) {
			camel_folder_summary_info_free(folder->summary, info);
			seq = i + 1;
			g_array_append_val (removed, seq);
			i--;
			summary_len--;
			continue;
		}
		
		/* Update summary flags */
		if (new[i].flags != iinfo->server_flags) {
			guint32 server_set, server_cleared;
			
			server_set = new[i].flags & ~iinfo->server_flags;
			server_cleared = iinfo->server_flags & ~new[i].flags;
			
			info->flags = (info->flags | server_set) & ~server_cleared;
			iinfo->server_flags = new[i].flags;

			if (changes == NULL)
				changes = camel_folder_change_info_new();
			camel_folder_change_info_change_uid(changes, new[i].uid);
		}
		
		camel_folder_summary_info_free (folder->summary, info);
		g_free (new[i].uid);
	}

	if (changes) {
		camel_object_trigger_event(CAMEL_OBJECT (folder), "folder_changed", changes);
		camel_folder_change_info_free(changes);
	}
	
	seq = i + 1;
	
	/* Free remaining memory. */
	while (i < summary_len && new[i].uid)
		g_free (new[i++].uid);
	g_free (new);
	
	/* Remove any leftover cached summary messages. (Yes, we
	 * repeatedly add the same number to the removed array.
	 * See RFC2060 7.4.1)
	 */
	for (i = seq; i <= summary_len; i++)
		g_array_append_val (removed, seq);
	
	/* And finally update the summary. */
	camel_imap_folder_changed (folder, exists, removed, ex);
	g_array_free (removed, TRUE);
}

/* the max number of chars that an unsigned 32-bit int can be is 10 chars plus 1 for a possible : */
#define UID_SET_FULL(setlen, maxlen) (maxlen > 0 ? setlen + 11 >= maxlen : FALSE)

/* Find all messages in @folder with flags matching @flags and @mask.
 * If no messages match, returns %NULL. Otherwise, returns an array of
 * CamelMessageInfo and sets *@set to a message set corresponding the
 * UIDs of the matched messages (up to @UID_SET_LIMIT bytes). The
 * caller must free the infos, the array, and the set string.
 */
static GPtrArray *
get_matching (CamelFolder *folder, guint32 flags, guint32 mask, char **set)
{
	GPtrArray *matches;
	CamelMessageInfo *info;
	int i, max, range;
	GString *gset;
	
	matches = g_ptr_array_new ();
	gset = g_string_new ("");
	max = camel_folder_summary_count (folder->summary);
	range = -1;
	for (i = 0; i < max && !UID_SET_FULL (gset->len, UID_SET_LIMIT); i++) {
		info = camel_folder_summary_index (folder->summary, i);
		if (!info)
			continue;
		if ((info->flags & mask) != flags) {
			camel_folder_summary_info_free (folder->summary, info);
			if (range != -1) {
				if (range != i - 1) {
					info = matches->pdata[matches->len - 1];
					g_string_sprintfa (gset, ":%s", camel_message_info_uid (info));
				}
				range = -1;
			}
			continue;
		}
		
		g_ptr_array_add (matches, info);
		if (range != -1)
			continue;
		range = i;
		if (gset->len)
			g_string_append_c (gset, ',');
		g_string_sprintfa (gset, "%s", camel_message_info_uid (info));
	}
	
	if (range != -1 && range != max - 1) {
		info = matches->pdata[matches->len - 1];
		g_string_sprintfa (gset, ":%s", camel_message_info_uid (info));
	}
	
	if (matches->len) {
		*set = gset->str;
		g_string_free (gset, FALSE);
		return matches;
	} else {
		g_string_free (gset, TRUE);
		g_ptr_array_free (matches, TRUE);
		return NULL;
	}
}

static void
imap_sync_offline (CamelFolder *folder, CamelException *ex)
{
	camel_folder_summary_save (folder->summary);
}

static void
imap_sync_online (CamelFolder *folder, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapResponse *response = NULL;
	CamelMessageInfo *info;
	GPtrArray *matches;
	char *set, *flaglist;
	gboolean unset;
	int i, j, max;
	
	CAMEL_IMAP_STORE_LOCK (store, command_lock);
	
	/* Find a message with changed flags, find all of the other
	 * messages like it, sync them as a group, mark them as
	 * updated, and continue.
	 */
	max = camel_folder_summary_count (folder->summary);
	for (i = 0; i < max; i++) {
		info = camel_folder_summary_index (folder->summary, i);
		if (!info)
			continue;
		if (!(info->flags & CAMEL_MESSAGE_FOLDER_FLAGGED)) {
			camel_folder_summary_info_free (folder->summary, info);
			continue;
		}
		
		/* Note: Cyrus is broken and will not accept an
		   empty-set of flags so... if this is true then we
		   want to unset the previously set flags.*/
		unset = !(info->flags & CAMEL_IMAP_SERVER_FLAGS);
		
		/* FIXME: since we don't know the previously set
		   flags, if unset is TRUE then just unset all the flags? */
		flaglist = imap_create_flag_list (unset ? CAMEL_IMAP_SERVER_FLAGS : info->flags);
		
		/* Note: get_matching() uses UID_SET_LIMIT to limit
		   the size of the uid-set string. We don't have to
		   loop here to flush all the matching uids because
		   they will be scooped up later by our parent loop (I
		   think?). -- Jeff */
		matches = get_matching (folder, info->flags & (CAMEL_IMAP_SERVER_FLAGS | CAMEL_MESSAGE_FOLDER_FLAGGED),
					CAMEL_IMAP_SERVER_FLAGS | CAMEL_MESSAGE_FOLDER_FLAGGED, &set);
		camel_folder_summary_info_free (folder->summary, info);
		
		/* Note: to `unset' flags, use -FLAGS.SILENT (<flag list>) */
		response = camel_imap_command (store, folder, ex,
					       "UID STORE %s %sFLAGS.SILENT %s",
					       set, unset ? "-" : "", flaglist);
		g_free (set);
		g_free (flaglist);
		
		if (response)
			camel_imap_response_free (store, response);
		
		if (!camel_exception_is_set (ex)) {
			for (j = 0; j < matches->len; j++) {
				info = matches->pdata[j];
				info->flags &= ~CAMEL_MESSAGE_FOLDER_FLAGGED;
				((CamelImapMessageInfo*)info)->server_flags =
					info->flags & CAMEL_IMAP_SERVER_FLAGS;
			}
			camel_folder_summary_touch (folder->summary);
		}
		
		for (j = 0; j < matches->len; j++) {
			info = matches->pdata[j];
			camel_folder_summary_info_free (folder->summary, info);
		}
		g_ptr_array_free (matches, TRUE);
		
		if (camel_exception_is_set (ex)) {
			CAMEL_IMAP_STORE_UNLOCK (store, command_lock);
			return;
		}
	}
	
	/* Save the summary */
	imap_sync_offline (folder, ex);
	
	CAMEL_IMAP_STORE_UNLOCK (store, command_lock);
}

static void
imap_expunge_uids_offline (CamelFolder *folder, GPtrArray *uids, CamelException *ex)
{
	int i;
	
	for (i = 0; i < uids->len; i++) {
		camel_folder_summary_remove_uid (folder->summary, uids->pdata[i]);
		/* We intentionally don't remove it from the cache because
		 * the cached data may be useful in replaying a COPY later.
		 */
	}
	camel_folder_summary_save (folder->summary);
	
	camel_disco_diary_log (CAMEL_DISCO_STORE (folder->parent_store)->diary,
			       CAMEL_DISCO_DIARY_FOLDER_EXPUNGE, folder, uids);
}

static void
imap_expunge_uids_online (CamelFolder *folder, GPtrArray *uids, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapResponse *response;
	int uid = 0;
	char *set;
	
	CAMEL_IMAP_STORE_LOCK (store, command_lock);
	
	while (uid < uids->len) {
		set = imap_uid_array_to_set (folder->summary, uids, uid, UID_SET_LIMIT, &uid);
		response = camel_imap_command (store, folder, ex,
					       "UID STORE %s +FLAGS.SILENT \\Deleted",
					       set);
		if (response)
			camel_imap_response_free (store, response);
		if (camel_exception_is_set (ex)) {
			CAMEL_IMAP_STORE_UNLOCK (store, command_lock);
			g_free (set);
			return;
		}
		
		if (store->capabilities & IMAP_CAPABILITY_UIDPLUS) {
			response = camel_imap_command (store, folder, ex,
						       "UID EXPUNGE %s", set);
		} else
			response = camel_imap_command (store, folder, ex, "EXPUNGE");
		
		if (response)
			camel_imap_response_free (store, response);
	}
	
	CAMEL_IMAP_STORE_UNLOCK (store, command_lock);
}

static int
uid_compar (const void *va, const void *vb)
{
	const char **sa = (const char **)va, **sb = (const char **)vb;
	unsigned long a, b;

	a = strtoul (*sa, NULL, 10);
	b = strtoul (*sb, NULL, 10);
	if (a < b)
		return -1;
	else if (a == b)
		return 0;
	else
		return 1;
}

static void
imap_expunge_uids_resyncing (CamelFolder *folder, GPtrArray *uids, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	GPtrArray *keep_uids, *mark_uids;
	CamelImapResponse *response;
	char *result;

	if (store->capabilities & IMAP_CAPABILITY_UIDPLUS) {
		imap_expunge_uids_online (folder, uids, ex);
		return;
	}
	
	/* If we don't have UID EXPUNGE we need to avoid expunging any
	 * of the wrong messages. So we search for deleted messages,
	 * and any that aren't in our to-expunge list get temporarily
	 * marked un-deleted.
	 */
	
	CAMEL_IMAP_STORE_LOCK (store, command_lock);
	response = camel_imap_command (store, folder, ex, "UID SEARCH DELETED");
	if (!response) {
		CAMEL_IMAP_STORE_UNLOCK (store, command_lock);
		return;
	}
	result = camel_imap_response_extract (store, response, "SEARCH", ex);
	if (!result) {
		CAMEL_IMAP_STORE_UNLOCK (store, command_lock);
		return;
	}
	
	if (result[8] == ' ') {
		char *uid, *lasts = NULL;
		unsigned long euid, kuid;
		int ei, ki;
		
		keep_uids = g_ptr_array_new ();
		mark_uids = g_ptr_array_new ();
		
		/* Parse SEARCH response */
		for (uid = strtok_r (result + 9, " ", &lasts); uid; uid = strtok_r (NULL, " ", &lasts))
			g_ptr_array_add (keep_uids, uid);
		qsort (keep_uids->pdata, keep_uids->len,
		       sizeof (void *), uid_compar);
		
		/* Fill in "mark_uids", empty out "keep_uids" as needed */
		for (ei = ki = 0; ei < uids->len; ei++) {
			euid = strtoul (uids->pdata[ei], NULL, 10);
			
			for (kuid = 0; ki < keep_uids->len; ki++) {
				kuid = strtoul (keep_uids->pdata[ki], NULL, 10);
				
				if (kuid >= euid)
					break;
			}
			
			if (euid == kuid)
				g_ptr_array_remove_index (keep_uids, ki);
			else
				g_ptr_array_add (mark_uids, uids->pdata[ei]);
		}
	} else {
		/* Empty SEARCH result, meaning nothing is marked deleted
		 * on server.
		 */
		
		keep_uids = NULL;
		mark_uids = NULL;
	}
	
	g_free (result);
	
	/* Unmark messages to be kept */
	if (keep_uids) {
		char *uidset;
		int uid = 0;
		
		while (uid < keep_uids->len) {
			uidset = imap_uid_array_to_set (folder->summary, keep_uids, uid, UID_SET_LIMIT, &uid);
			
			response = camel_imap_command (store, folder, ex,
						       "UID STORE %s -FLAGS.SILENT \\Deleted",
						       uidset);
			
			g_free (uidset);
			
			if (!response) {
				g_ptr_array_free (keep_uids, TRUE);
				g_ptr_array_free (mark_uids, TRUE);
				CAMEL_IMAP_STORE_UNLOCK (store, command_lock);
				return;
			}
			camel_imap_response_free (store, response);
		}
	}
	
	/* Mark any messages that still need to be marked */
	if (mark_uids) {
		char *uidset;
		int uid = 0;
		
		while (uid < mark_uids->len) {
			uidset = imap_uid_array_to_set (folder->summary, mark_uids, uid, UID_SET_LIMIT, &uid);
			
			response = camel_imap_command (store, folder, ex,
						       "UID STORE %s +FLAGS.SILENT \\Deleted",
						       uidset);
			
			g_free (uidset);
			
			if (!response) {
				g_ptr_array_free (keep_uids, TRUE);
				g_ptr_array_free (mark_uids, TRUE);
				CAMEL_IMAP_STORE_UNLOCK (store, command_lock);
				return;
			}
			camel_imap_response_free (store, response);
		}
		
		g_ptr_array_free (mark_uids, TRUE);
	}
	
	/* Do the actual expunging */
	response = camel_imap_command (store, folder, ex, "EXPUNGE");
	if (response)
		camel_imap_response_free (store, response);
	
	/* And fix the remaining messages if we mangled them */
	if (keep_uids) {
		char *uidset;
		int uid = 0;
		
		while (uid < keep_uids->len) {
			uidset = imap_uid_array_to_set (folder->summary, keep_uids, uid, UID_SET_LIMIT, &uid);
			
			/* Don't pass ex if it's already been set */
			response = camel_imap_command (store, folder,
						       camel_exception_is_set (ex) ? NULL : ex,
						       "UID STORE %s +FLAGS.SILENT \\Deleted",
						       uidset);
			
			g_free (uidset);
			if (response)
				camel_imap_response_free (store, response);
		}
		
		g_ptr_array_free (keep_uids, TRUE);
	}
	
	CAMEL_IMAP_STORE_UNLOCK (store, command_lock);
}

static void
imap_append_offline (CamelFolder *folder, CamelMimeMessage *message,
		     const CamelMessageInfo *info, CamelException *ex)
{
	CamelImapStore *imap_store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapMessageCache *cache = CAMEL_IMAP_FOLDER (folder)->cache;
	CamelFolderChangeInfo *changes;
	char *uid;

	/* We could keep a separate counter, but this one works fine. */
	CAMEL_IMAP_STORE_LOCK (imap_store, command_lock);
	uid = g_strdup_printf ("append-%d", imap_store->command++);
	CAMEL_IMAP_STORE_UNLOCK (imap_store, command_lock);

	camel_imap_summary_add_offline (folder->summary, uid, message, info);
	CAMEL_IMAP_FOLDER_LOCK (folder, cache_lock);
	camel_imap_message_cache_insert_wrapper (cache, uid, "",
						 CAMEL_DATA_WRAPPER (message), ex);
	CAMEL_IMAP_FOLDER_UNLOCK (folder, cache_lock);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, uid);
	camel_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed",
				    changes);
	camel_folder_change_info_free (changes);

	camel_disco_diary_log (CAMEL_DISCO_STORE (imap_store)->diary,
			       CAMEL_DISCO_DIARY_FOLDER_APPEND, folder, uid);
	g_free (uid);
}

static CamelImapResponse *
do_append (CamelFolder *folder, CamelMimeMessage *message,
	   const CamelMessageInfo *info, char **uid,
	   CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapResponse *response;
	CamelStream *memstream;
	CamelMimeFilter *crlf_filter;
	CamelStreamFilter *streamfilter;
	GByteArray *ba;
	char *flagstr, *result, *end;
	
	/* create flag string param */
	if (info && info->flags)
		flagstr = imap_create_flag_list (info->flags);
	else
		flagstr = NULL;
	
	/* encode any 8bit parts so we avoid sending embedded nul-chars and such  */
	/* commented out because it might change the encoding on
           signed parts which'd break stuff */
	/*camel_mime_message_encode_8bit_parts (message);*/
	
	/* FIXME: We could avoid this if we knew how big the message was. */
	memstream = camel_stream_mem_new ();
	ba = g_byte_array_new ();
	camel_stream_mem_set_byte_array (CAMEL_STREAM_MEM (memstream), ba);
	
	streamfilter = camel_stream_filter_new_with_stream (memstream);
	crlf_filter = camel_mime_filter_crlf_new (CAMEL_MIME_FILTER_CRLF_ENCODE,
						  CAMEL_MIME_FILTER_CRLF_MODE_CRLF_ONLY);
	camel_stream_filter_add (streamfilter, crlf_filter);
	camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (message),
					    CAMEL_STREAM (streamfilter));
	camel_object_unref (CAMEL_OBJECT (streamfilter));
	camel_object_unref (CAMEL_OBJECT (crlf_filter));
	camel_object_unref (CAMEL_OBJECT (memstream));
	
	response = camel_imap_command (store, NULL, ex, "APPEND %F%s%s {%d}",
				       folder->full_name, flagstr ? " " : "",
				       flagstr ? flagstr : "", ba->len);
	g_free (flagstr);
	
	if (!response) {
		g_byte_array_free (ba, TRUE);
		return NULL;
	}
	
	result = camel_imap_response_extract_continuation (store, response, ex);
	if (!result) {
		g_byte_array_free (ba, TRUE);
		return NULL;
	}
	g_free (result);
	
	/* send the rest of our data - the mime message */
	response = camel_imap_command_continuation (store, ba->data, ba->len, ex);
	g_byte_array_free (ba, TRUE);
	if (!response)
		return response;
	
	if (store->capabilities & IMAP_CAPABILITY_UIDPLUS) {
		*uid = strstrcase (response->status, "[APPENDUID ");
		if (*uid)
			*uid = strchr (*uid + 11, ' ');
		if (*uid) {
			*uid = g_strndup (*uid + 1, strcspn (*uid + 1, "]"));
			/* Make sure it's a number */
			if (strtoul (*uid, &end, 10) == 0 || *end) {
				g_free (*uid);
				*uid = NULL;
			}
		}
	} else
		*uid = NULL;
	
	return response;
}

static void
imap_append_online (CamelFolder *folder, CamelMimeMessage *message,
		    const CamelMessageInfo *info, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapResponse *response;
	char *uid;
	int count;
	
	count = camel_folder_summary_count (folder->summary);
	response = do_append (folder, message, info, &uid, ex);
	if (!response)
		return;
	
	if (uid) {
		/* Cache first, since freeing response may trigger a
		 * summary update that will want this information.
		 */
		CAMEL_IMAP_FOLDER_LOCK (folder, cache_lock);
		camel_imap_message_cache_insert_wrapper (
			CAMEL_IMAP_FOLDER (folder)->cache, uid,
			"", CAMEL_DATA_WRAPPER (message), ex);
		CAMEL_IMAP_FOLDER_UNLOCK (folder, cache_lock);
		g_free (uid);
	}
	
	camel_imap_response_free (store, response);
	
	/* Make sure a "folder_changed" is emitted. */
	if (store->current_folder != folder ||
	    camel_folder_summary_count (folder->summary) == count)
		imap_refresh_info (folder, ex);
}

static void
imap_append_resyncing (CamelFolder *folder, CamelMimeMessage *message,
		       const CamelMessageInfo *info, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapResponse *response;
	char *uid;
	
	response = do_append (folder, message, info, &uid, ex);
	if (!response)
		return;
	
	if (uid) {
		CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
		const char *olduid = camel_message_info_uid (info);
		
		CAMEL_IMAP_FOLDER_LOCK (imap_folder, cache_lock);
		camel_imap_message_cache_copy (imap_folder->cache, olduid,
					       imap_folder->cache, uid, ex);
		CAMEL_IMAP_FOLDER_UNLOCK (imap_folder, cache_lock);
		
		camel_disco_diary_uidmap_add (CAMEL_DISCO_STORE (store)->diary,
					      olduid, uid);
	}
	
	camel_imap_response_free (store, response);
}


static void
imap_copy_offline (CamelFolder *source, GPtrArray *uids,
		   CamelFolder *destination, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (source->parent_store);
	CamelImapMessageCache *sc = CAMEL_IMAP_FOLDER (source)->cache;
	CamelImapMessageCache *dc = CAMEL_IMAP_FOLDER (destination)->cache;
	CamelFolderChangeInfo *changes;
	CamelMimeMessage *message;
	CamelMessageInfo *mi;
	char *uid, *destuid;
	int i;

	/* We grab the store's command lock first, and then grab the
	 * source and destination cache_locks. This way we can't
	 * deadlock in the case where we're simultaneously also trying
	 * to copy messages in the other direction from another thread.
	 */
	CAMEL_IMAP_STORE_LOCK (store, command_lock);
	CAMEL_IMAP_FOLDER_LOCK (source, cache_lock);
	CAMEL_IMAP_FOLDER_LOCK (destination, cache_lock);
	CAMEL_IMAP_STORE_UNLOCK (store, command_lock);

	changes = camel_folder_change_info_new ();
	for (i = 0; i < uids->len; i++) {
		uid = uids->pdata[i];

		message = camel_folder_get_message (source, uid, NULL);
		if (!message)
			continue;
		mi = camel_folder_summary_uid (source->summary, uid);
		g_return_if_fail (mi != NULL);

		destuid = g_strdup_printf ("copy-%s:%s", source->full_name, uid);
		camel_imap_summary_add_offline (destination->summary, destuid, message, mi);

		camel_imap_message_cache_copy (sc, uid, dc, destuid, ex);
		camel_folder_summary_info_free (source->summary, mi);
		camel_object_unref (CAMEL_OBJECT (message));

		camel_folder_change_info_add_uid (changes, destuid);
		g_free (destuid);
	}

	CAMEL_IMAP_FOLDER_UNLOCK (destination, cache_lock);
	CAMEL_IMAP_FOLDER_UNLOCK (source, cache_lock);

	camel_object_trigger_event (CAMEL_OBJECT (destination),
				    "folder_changed", changes);
	camel_folder_change_info_free (changes);

	camel_disco_diary_log (CAMEL_DISCO_STORE (store)->diary,
			       CAMEL_DISCO_DIARY_FOLDER_COPY,
			       source, destination, uids);
}
	
static void
handle_copyuid (CamelImapResponse *response, CamelFolder *source,
		CamelFolder *destination)
{
	CamelImapMessageCache *scache = CAMEL_IMAP_FOLDER (source)->cache;
	CamelImapMessageCache *dcache = CAMEL_IMAP_FOLDER (destination)->cache;
	char *validity, *srcset, *destset;
	GPtrArray *src, *dest;
	int i;

	validity = strstrcase (response->status, "[COPYUID ");
	if (!validity)
		return;
	validity += 9;
	if (strtoul (validity, NULL, 10) !=
	    CAMEL_IMAP_SUMMARY (destination->summary)->validity)
		return;

	srcset = strchr (validity, ' ');
	if (!srcset++)
		goto lose;
	destset = strchr (srcset, ' ');
	if (!destset++)
		goto lose;

	src = imap_uid_set_to_array (source->summary, srcset);
	dest = imap_uid_set_to_array (destination->summary, destset);

	if (src && dest && src->len == dest->len) {
		/* We don't have to worry about deadlocking on the
		 * cache locks here, because we've got the store's
		 * command lock too, so no one else could be here.
		 */
		CAMEL_IMAP_FOLDER_LOCK (source, cache_lock);
		CAMEL_IMAP_FOLDER_LOCK (destination, cache_lock);
		for (i = 0; i < src->len; i++) {
			camel_imap_message_cache_copy (scache, src->pdata[i],
						       dcache, dest->pdata[i],
						       NULL);
		}
		CAMEL_IMAP_FOLDER_UNLOCK (source, cache_lock);
		CAMEL_IMAP_FOLDER_UNLOCK (destination, cache_lock);

		imap_uid_array_free (src);
		imap_uid_array_free (dest);
		return;
	}

	imap_uid_array_free (src);
	imap_uid_array_free (dest);
 lose:
	g_warning ("Bad COPYUID response from server");
}

static void
do_copy (CamelFolder *source, GPtrArray *uids,
	 CamelFolder *destination, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (source->parent_store);
	CamelImapResponse *response;
	char *uidset;
	int uid = 0;
	
	while (uid < uids->len && !camel_exception_is_set (ex)) {
		uidset = imap_uid_array_to_set (source->summary, uids, uid, UID_SET_LIMIT, &uid);
		
		response = camel_imap_command (store, source, ex, "UID COPY %s %F",
					       uidset, destination->full_name);
		
		g_free (uidset);
		
		if (response && (store->capabilities & IMAP_CAPABILITY_UIDPLUS))
			handle_copyuid (response, source, destination);
		
		camel_imap_response_free (store, response);
	}
}

static void
imap_copy_online (CamelFolder *source, GPtrArray *uids,
		  CamelFolder *destination, CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (source->parent_store);
	int count;

	/* Sync message flags if needed. */
	imap_sync_online (source, ex);
	if (camel_exception_is_set (ex))
		return;

	count = camel_folder_summary_count (destination->summary);

	/* Now copy the messages */
	do_copy (source, uids, destination, ex);
	if (camel_exception_is_set (ex))
		return;

	/* Make the destination notice its new messages */
	if (store->current_folder != destination ||
	    camel_folder_summary_count (destination->summary) == count)
		camel_folder_refresh_info (destination, ex);
}

static void
imap_copy_resyncing (CamelFolder *source, GPtrArray *uids,
		     CamelFolder *destination, CamelException *ex)
{
	CamelDiscoDiary *diary = CAMEL_DISCO_STORE (source->parent_store)->diary;
	GPtrArray *realuids;
	int first, i;
	const char *uid;
	CamelMimeMessage *message;
	CamelMessageInfo *info;

	/* This is trickier than append_resyncing, because some of
	 * the messages we are copying may have been copied or
	 * appended into @source while we were offline, in which case
	 * if we don't have UIDPLUS, we won't know their real UIDs,
	 * so we'll have to append them rather than copying.
	 */

	realuids = g_ptr_array_new ();

	i = 0;
	while (i < uids->len) {
		/* Skip past real UIDs */
		for (first = i; i < uids->len; i++) {
			uid = uids->pdata[i];

			if (!isdigit ((unsigned char)*uid)) {
				uid = camel_disco_diary_uidmap_lookup (diary, uid);
				if (!uid)
					break;
			}
			g_ptr_array_add (realuids, (char *)uid);
		}

		/* If we saw any real UIDs, do a COPY */
		if (i != first) {
			do_copy (source, realuids, destination, ex);
			g_ptr_array_set_size (realuids, 0);
			if (i == uids->len || camel_exception_is_set (ex))
				break;
		}

		/* Deal with fake UIDs */
		while (i < uids->len &&
		       !isdigit (*(unsigned char *)(uids->pdata[i])) &&
		       !camel_exception_is_set (ex)) {
			message = camel_folder_get_message (source, uids->pdata[i], NULL);
			if (!message) {
				/* Message must have been expunged */
				continue;
			}
			info = camel_folder_get_message_info (source, uids->pdata[i]);
			g_return_if_fail (info != NULL);

			imap_append_online (destination, message, info, ex);
			camel_folder_free_message_info (source, info);
			camel_object_unref (CAMEL_OBJECT (message));
			i++;
		}
	}

	g_ptr_array_free (realuids, FALSE);
}

static void
imap_move_messages_to (CamelFolder *source, GPtrArray *uids,
		       CamelFolder *destination, CamelException *ex)
{
	int i;

	/* do it this way (as opposed to camel_folder_copy_messages_to)
	 * to avoid locking issues */
	CF_CLASS (source)->copy_messages_to (source, uids, destination, ex);
	if (camel_exception_is_set (ex))
		return;

	for (i = 0; i < uids->len; i++)
		camel_folder_delete_message (source, uids->pdata[i]);
}

static GPtrArray *
imap_search_by_expression (CamelFolder *folder, const char *expression, CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	GPtrArray *matches, *summary;

	/* we could get around this by creating a new search object each time,
	   but i doubt its worth it since any long operation would lock the
	   command channel too */
	CAMEL_IMAP_FOLDER_LOCK(folder, search_lock);

	camel_folder_search_set_folder (imap_folder->search, folder);
	summary = camel_folder_get_summary(folder);
	camel_folder_search_set_summary(imap_folder->search, summary);
	matches = camel_folder_search_execute_expression (imap_folder->search, expression, ex);

	CAMEL_IMAP_FOLDER_UNLOCK(folder, search_lock);

	camel_folder_free_summary(folder, summary);

	return matches;
}

static GPtrArray *
imap_search_by_uids(CamelFolder *folder, const char *expression, GPtrArray *uids, CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER(folder);
	GPtrArray *summary, *matches;
	int i;

	/* NOTE: could get away without the search lock by creating a new
	   search object each time */

	summary = g_ptr_array_new();
	for (i=0;i<uids->len;i++) {
		CamelMessageInfo *info;

		info = camel_folder_get_message_info(folder, uids->pdata[i]);
		if (info)
			g_ptr_array_add(summary, info);
	}

	if (summary->len == 0)
		return summary;

	CAMEL_IMAP_FOLDER_LOCK(folder, search_lock);

	camel_folder_search_set_folder(imap_folder->search, folder);
	camel_folder_search_set_summary(imap_folder->search, summary);

	matches = camel_folder_search_execute_expression(imap_folder->search, expression, ex);

	CAMEL_IMAP_FOLDER_UNLOCK(folder, search_lock);

	for (i=0;i<summary->len;i++)
		camel_folder_free_message_info(folder, summary->pdata[i]);
	g_ptr_array_free(summary, TRUE);

	return matches;
}

static void
imap_search_free (CamelFolder *folder, GPtrArray *uids)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);

	g_return_if_fail (imap_folder->search);

	CAMEL_IMAP_FOLDER_LOCK(folder, search_lock);

	camel_folder_search_free_result (imap_folder->search, uids);

	CAMEL_IMAP_FOLDER_UNLOCK(folder, search_lock);
}

static CamelMimeMessage *get_message (CamelImapFolder *imap_folder,
				      const char *uid,
				      const char *part_specifier,
				      CamelMessageContentInfo *ci,
				      CamelException *ex);

/* Fetch the contents of the MIME part indicated by @ci, which is part
 * of message @uid in @folder.
 */
static CamelDataWrapper *
get_content (CamelImapFolder *imap_folder, const char *uid,
	     const char *part_spec, CamelMimePart *part,
	     CamelMessageContentInfo *ci, CamelException *ex)
{
	CamelDataWrapper *content = NULL;
	CamelStream *stream;
	char *child_spec;
	
	/* There are three cases: multipart, message/rfc822, and "other" */
	
	if (header_content_type_is (ci->type, "multipart", "*")) {
		CamelMultipart *body_mp;
		int speclen, num;
		
		body_mp = camel_multipart_new ();
		camel_data_wrapper_set_mime_type_field (
			CAMEL_DATA_WRAPPER (body_mp), ci->type);
		
		camel_multipart_set_boundary (body_mp, NULL);
		
		speclen = strlen (part_spec);
		child_spec = g_malloc (speclen + 15);
		memcpy (child_spec, part_spec, speclen);
		if (speclen > 0)
			child_spec[speclen++] = '.';
		
		ci = ci->childs;
		num = 1;
		while (ci) {
			sprintf (child_spec + speclen, "%d.MIME", num++);
			stream = camel_imap_folder_fetch_data (imap_folder, uid, child_spec, FALSE, ex);
			if (stream) {
				int ret;
				
				part = camel_mime_part_new ();
				ret = camel_data_wrapper_construct_from_stream (CAMEL_DATA_WRAPPER (part), stream);
				camel_object_unref (CAMEL_OBJECT (stream));
				if (ret == -1) {
					camel_object_unref (CAMEL_OBJECT (part));
					camel_object_unref (CAMEL_OBJECT (body_mp));
					g_free (child_spec);
					return NULL;
				}
				
				*(strchr (child_spec + speclen, '.')) = '\0';
				content = get_content (imap_folder, uid, child_spec, part, ci, ex);
			}
			if (!stream || !content) {
				g_free (child_spec);
				camel_object_unref (CAMEL_OBJECT (body_mp));
				return NULL;
			}
			
			camel_medium_set_content_object (CAMEL_MEDIUM (part), content);
			camel_object_unref (CAMEL_OBJECT (content));
			camel_multipart_add_part (body_mp, part);
			camel_object_unref (CAMEL_OBJECT (part));
			
			ci = ci->next;
		}
		g_free (child_spec);

		return (CamelDataWrapper *)body_mp;
	} else if (header_content_type_is (ci->type, "message", "rfc822")) {
		return (CamelDataWrapper *)
			get_message (imap_folder, uid, part_spec, ci->childs, ex);
	} else {
		if (!ci->parent || header_content_type_is (ci->parent->type, "message", "rfc822"))
			child_spec = g_strdup_printf ("%s%s1", part_spec, *part_spec ? "." : "");
		else
			child_spec = g_strdup (part_spec);

		content = camel_imap_wrapper_new (imap_folder, ci->type, uid, child_spec, part);
		g_free (child_spec);
		return content;
	}
}

static CamelMimeMessage *
get_message (CamelImapFolder *imap_folder, const char *uid,
	     const char *part_spec, CamelMessageContentInfo *ci,
	     CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (CAMEL_FOLDER (imap_folder)->parent_store);
	CamelDataWrapper *content;
	CamelMimeMessage *msg;
	CamelStream *stream;
	char *section_text;
	int ret;
	
	section_text = g_strdup_printf ("%s%s%s", part_spec, *part_spec ? "." : "",
					store->server_level >= IMAP_LEVEL_IMAP4REV1 ? "HEADER" : "0");
	stream = camel_imap_folder_fetch_data (imap_folder, uid, section_text, FALSE, ex);
	g_free (section_text);
	if (!stream)
		return NULL;
	
	msg = camel_mime_message_new ();
	ret = camel_data_wrapper_construct_from_stream (CAMEL_DATA_WRAPPER (msg), stream);
	camel_object_unref (CAMEL_OBJECT (stream));
	if (ret == -1) {
		camel_object_unref (CAMEL_OBJECT (msg));
		return NULL;
	}
	
	content = get_content (imap_folder, uid, part_spec, CAMEL_MIME_PART (msg), ci, ex);
	if (!content) {
		camel_object_unref (CAMEL_OBJECT (msg));
		return NULL;
	}
	
	camel_medium_set_content_object (CAMEL_MEDIUM (msg), content);
	camel_object_unref (CAMEL_OBJECT (content));
	
	return msg;
}

/* FIXME: I pulled this number out of my butt. */
#define IMAP_SMALL_BODY_SIZE 5120

static CamelMimeMessage *
get_message_simple (CamelImapFolder *imap_folder, const char *uid,
		    CamelStream *stream, CamelException *ex)
{
	CamelMimeMessage *msg;
	CamelImapStore *imap_store =
		CAMEL_IMAP_STORE (CAMEL_FOLDER (imap_folder)->parent_store);
	int ret;
	
	if (!stream) {
		stream = camel_imap_folder_fetch_data (imap_folder, uid, "",
						       FALSE, ex);
		if (!stream)
			return NULL;
	}

	msg = camel_mime_message_new ();
	ret = camel_data_wrapper_construct_from_stream (CAMEL_DATA_WRAPPER (msg),
							stream);
	camel_object_unref (CAMEL_OBJECT (stream));
	if (ret == -1) {
		camel_object_unref (CAMEL_OBJECT (msg));
		return NULL;
	}

	/* FIXME, this shouldn't be done this way. */
	camel_medium_set_header (CAMEL_MEDIUM (msg), "X-Evolution-Source",
				 imap_store->base_url);
	return msg;
}

static CamelMimeMessage *
imap_get_message (CamelFolder *folder, const char *uid, CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelMessageInfo *mi;
	CamelMimeMessage *msg;
	CamelStream *stream = NULL;
	
	/* If the server doesn't support IMAP4rev1, or we already have
	 * the whole thing cached, fetch it in one piece.
	 */
	if (store->server_level < IMAP_LEVEL_IMAP4REV1 ||
	    (stream = camel_imap_folder_fetch_data (imap_folder, uid, "", TRUE, NULL)))
		return get_message_simple (imap_folder, uid, stream, ex);
	
	mi = camel_folder_summary_uid (folder->summary, uid);
	g_return_val_if_fail (mi != NULL, NULL);
	
	/* If the message is small, fetch it in one piece. */
	if (mi->size < IMAP_SMALL_BODY_SIZE) {
		camel_folder_summary_info_free (folder->summary, mi);
		return get_message_simple (imap_folder, uid, NULL, ex);
	}
	
	/* For larger messages, fetch the structure and build a message
	 * with offline parts. (We check mi->content->type rather than
	 * mi->content because camel_folder_summary_info_new always creates
	 * an empty content struct.)
	 */
	if (!mi->content->type) {
		CamelImapResponse *response;
		GData *fetch_data = NULL;
		char *body, *found_uid;
		int i;
		
		if (camel_disco_store_status (CAMEL_DISCO_STORE (store)) == CAMEL_DISCO_STORE_OFFLINE) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					     _("This message is not currently available"));
			return NULL;
		}
		
		response = camel_imap_command (store, folder, ex,
					       "UID FETCH %s BODY", uid);
		if (!response) {
			camel_folder_summary_info_free (folder->summary, mi);
			return NULL;
		}
		
		for (i = 0, body = NULL; i < response->untagged->len; i++) {
			fetch_data = parse_fetch_response (imap_folder, response->untagged->pdata[i]);
			if (fetch_data) {
				found_uid = g_datalist_get_data (&fetch_data, "UID");
				body = g_datalist_get_data (&fetch_data, "BODY");
				if (found_uid && body && !strcmp (found_uid, uid))
					break;
				g_datalist_clear (&fetch_data);
				fetch_data = NULL;
				body = NULL;
			}
		}
		
		if (body)
			imap_parse_body (&body, folder, mi->content);
		
		if (fetch_data)
			g_datalist_clear (&fetch_data);
		
		camel_imap_response_free (store, response);
		
		if (!mi->content->type) {
			/* FETCH returned OK, but we didn't parse a BODY
			 * response. Courier will return invalid BODY
			 * responses for invalidly MIMEd messages, so
			 * fall back to fetching the entire thing and
			 * let the mailer's "bad MIME" code handle it.
			 */
			camel_folder_summary_info_free (folder->summary, mi);
			return get_message_simple (imap_folder, uid, NULL, ex);
		}
	}
	
	msg = get_message (imap_folder, uid, "", mi->content, ex);
	/* FIXME, this shouldn't be done this way. */
	camel_medium_set_header (CAMEL_MEDIUM (msg), "X-Evolution-Source",
				 store->base_url);
	camel_folder_summary_info_free (folder->summary, mi);
	
	return msg;
}

static void
imap_cache_message (CamelDiscoFolder *disco_folder, const char *uid,
		    CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (disco_folder);
	CamelStream *stream;

	stream = camel_imap_folder_fetch_data (imap_folder, uid, "", FALSE, ex);
	if (stream)
		camel_object_unref (CAMEL_OBJECT (stream));
}

/* We pretend that a FLAGS or RFC822.SIZE response is always exactly
 * 20 bytes long, and a BODY[HEADERS] response is always 2000 bytes
 * long. Since we know how many of each kind of response we're
 * expecting, we can find the total (pretend) amount of server traffic
 * to expect and then count off the responses as we read them to update
 * the progress bar.
 */
#define IMAP_PRETEND_SIZEOF_FLAGS	  20
#define IMAP_PRETEND_SIZEOF_SIZE	  20
#define IMAP_PRETEND_SIZEOF_HEADERS	2000

static void
add_message_from_data (CamelFolder *folder, GPtrArray *messages,
		       int first, GData *data)
{
	CamelMimeMessage *msg;
	CamelStream *stream;
	CamelMessageInfo *mi;
	int seq;
	
	seq = GPOINTER_TO_INT (g_datalist_get_data (&data, "SEQUENCE"));
	if (seq < first)
		return;
	stream = g_datalist_get_data (&data, "BODY_PART_STREAM");
	if (!stream)
		return;
	
	if (seq - first >= messages->len)
		g_ptr_array_set_size (messages, seq - first + 1);
	
	msg = camel_mime_message_new ();
	if (camel_data_wrapper_construct_from_stream (CAMEL_DATA_WRAPPER (msg), stream) == -1) {
		camel_object_unref (CAMEL_OBJECT (msg));
		return;
	}
	
	mi = camel_folder_summary_info_new_from_message (folder->summary, msg);
	camel_object_unref (CAMEL_OBJECT (msg));
	
	messages->pdata[seq - first] = mi;
}

static void
imap_update_summary (CamelFolder *folder, int exists,
		     CamelFolderChangeInfo *changes,
		     CamelException *ex)
{
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	GPtrArray *fetch_data = NULL, *messages = NULL, *needheaders;
	guint32 flags, uidval, maxuid;
	int i, seq, first, size, got;
	CamelImapResponseType type;
	const char *header_spec;
	CamelMessageInfo *mi;
	CamelStream *stream;
	char *uid, *resp;
	GData *data;
	
	CAMEL_IMAP_STORE_ASSERT_LOCKED (store, command_lock);
	if (store->server_level >= IMAP_LEVEL_IMAP4REV1)
		header_spec = "HEADER";
	else
		header_spec = "0";
	
	/* Figure out if any of the new messages are already cached (which
	 * may be the case if we're re-syncing after disconnected operation).
	 * If so, get their UIDs, FLAGS, and SIZEs. If not, get all that
	 * and ask for the headers too at the same time.
	 */
	seq = camel_folder_summary_count (folder->summary);
	first = seq + 1;
	if (seq > 0) {
		mi = camel_folder_summary_index (folder->summary, seq - 1);
		uidval = atoi (camel_message_info_uid (mi));
		camel_folder_summary_info_free (folder->summary, mi);
	} else
		uidval = 0;
	
	size = (exists - seq) * (IMAP_PRETEND_SIZEOF_FLAGS + IMAP_PRETEND_SIZEOF_SIZE);
	got = 0;
	
	maxuid = camel_imap_message_cache_max_uid (imap_folder->cache);
	if (uidval >= maxuid) {
		/* None of the new messages are cached */
		size += (exists - seq) * IMAP_PRETEND_SIZEOF_HEADERS;
		if (!camel_imap_command_start (store, folder, ex,
					       "UID FETCH %d:* (FLAGS RFC822.SIZE BODY.PEEK[%s])",
					       maxuid + 1, header_spec))
			return;
		camel_operation_start (NULL, _("Fetching summary information for new messages"));
	} else {
		if (!camel_imap_command_start (store, folder, ex,
					       "UID FETCH %d:* (FLAGS RFC822.SIZE)",
					       uidval + 1))
			return;
		camel_operation_start (NULL, _("Scanning for new messages"));
	}
	
	/* Parse the responses. We can't add a message to the summary
	 * until we've gotten its headers, and there's no guarantee
	 * the server will send the responses in a useful order...
	 */
	fetch_data = g_ptr_array_new ();
	messages = g_ptr_array_new ();
	while ((type = camel_imap_command_response (store, &resp, ex)) ==
	       CAMEL_IMAP_RESPONSE_UNTAGGED) {
		data = parse_fetch_response (imap_folder, resp);
		g_free (resp);
		if (!data)
			continue;
		
		seq = GPOINTER_TO_INT (g_datalist_get_data (&data, "SEQUENCE"));
		if (seq < first) {
			g_datalist_clear (&data);
			continue;
		}
		
		if (g_datalist_get_data (&data, "FLAGS"))
			got += IMAP_PRETEND_SIZEOF_FLAGS;
		if (g_datalist_get_data (&data, "RFC822.SIZE"))
			got += IMAP_PRETEND_SIZEOF_SIZE;
		stream = g_datalist_get_data (&data, "BODY_PART_STREAM");
		if (stream) {
			got += IMAP_PRETEND_SIZEOF_HEADERS;
			
			/* Use the stream now so we don't tie up many
			 * many fds if we're fetching many many messages.
			 */
			add_message_from_data (folder, messages, first, data);
			g_datalist_set_data (&data, "BODY_PART_STREAM", NULL);
		}
		
		camel_operation_progress (NULL, got * 100 / size);
		g_ptr_array_add (fetch_data, data);
	}
	camel_operation_end (NULL);
	
	if (type == CAMEL_IMAP_RESPONSE_ERROR)
		goto lose;
	
	/* Figure out which headers we still need to fetch. */
	needheaders = g_ptr_array_new ();
	size = got = 0;
	for (i = 0; i < fetch_data->len; i++) {
		data = fetch_data->pdata[i];
		if (g_datalist_get_data (&data, "BODY_PART_LEN"))
			continue;
		
		uid = g_datalist_get_data (&data, "UID");
		if (uid) {
			g_ptr_array_add (needheaders, uid);
			size += IMAP_PRETEND_SIZEOF_HEADERS;
		}
	}
	
	/* And fetch them */
	if (needheaders->len) {
		char *uidset;
		int uid = 0;
		
		qsort (needheaders->pdata, needheaders->len,
		       sizeof (void *), uid_compar);
		
		camel_operation_start (NULL, _("Fetching summary information for new messages"));
		
		while (uid < needheaders->len) {
			uidset = imap_uid_array_to_set (folder->summary, needheaders, uid, UID_SET_LIMIT, &uid);
			if (!camel_imap_command_start (store, folder, ex,
						       "UID FETCH %s BODY.PEEK[%s]",
						       uidset, header_spec)) {
				g_ptr_array_free (needheaders, TRUE);
				camel_operation_end (NULL);
				g_free (uidset);
				goto lose;
			}
			g_free (uidset);
			
			while ((type = camel_imap_command_response (store, &resp, ex))
			       == CAMEL_IMAP_RESPONSE_UNTAGGED) {
				data = parse_fetch_response (imap_folder, resp);
				g_free (resp);
				if (!data)
					continue;
				
				stream = g_datalist_get_data (&data, "BODY_PART_STREAM");
				if (stream) {
					add_message_from_data (folder, messages, first, data);
					got += IMAP_PRETEND_SIZEOF_HEADERS;
					camel_operation_progress (NULL, got * 100 / size);
				}
				g_datalist_clear (&data);
			}
			
			if (type == CAMEL_IMAP_RESPONSE_ERROR) {
				g_ptr_array_free (needheaders, TRUE);
				camel_operation_end (NULL);
				goto lose;
			}
		}
		
		g_ptr_array_free (needheaders, TRUE);
		camel_operation_end (NULL);
	}
	
	/* Now finish up summary entries (fix UIDs, set flags and size) */
	for (i = 0; i < fetch_data->len; i++) {
		data = fetch_data->pdata[i];
		
		seq = GPOINTER_TO_INT (g_datalist_get_data (&data, "SEQUENCE"));
		if (seq >= first + messages->len) {
			g_datalist_clear (&data);
			continue;
		}
		
		mi = messages->pdata[seq - first];
		if (mi == NULL) {
			CamelMessageInfo *pmi = NULL;
			int j;
			
			/* This is a kludge around a bug in Exchange
			 * 5.5 that sometimes claims multiple messages
			 * have the same UID. See bug #17694 for
			 * details. The "solution" is to create a fake
			 * message-info with the same details as the
			 * previously valid message. Yes, the user
			 * will have a clone in his/her message-list,
			 * but at least we don't crash.
			 */
			
			/* find the previous valid message info */
			for (j = seq - first - 1; j >= 0; j--) {
				pmi = messages->pdata[j];
				if (pmi != NULL)
					break;
			}
			
			if (pmi == NULL) {
				/* Server response is *really* fucked up,
				   I guess we just pretend it never happened? */
				continue;
			}
			
			mi = camel_message_info_new ();
			camel_message_info_dup_to (pmi, mi);
		}
		
		uid = g_datalist_get_data (&data, "UID");
		if (uid)
			camel_message_info_set_uid (mi, g_strdup (uid));
		flags = GPOINTER_TO_INT (g_datalist_get_data (&data, "FLAGS"));
		if (flags) {
			((CamelImapMessageInfo *)mi)->server_flags = flags;
			/* "or" them in with the existing flags that may
			 * have been set by summary_info_new_from_message.
			 */
			mi->flags |= flags;
		}
		size = GPOINTER_TO_INT (g_datalist_get_data (&data, "RFC822.SIZE"));
		if (size)
			mi->size = size;
		
		g_datalist_clear (&data);
	}
	g_ptr_array_free (fetch_data, TRUE);
	
	/* And add the entries to the summary, etc. */
	for (i = 0; i < messages->len; i++) {
		mi = messages->pdata[i];
		if (!mi) {
			g_warning ("No information for message %d", i + first);
			continue;
		}
		camel_folder_summary_add (folder->summary, mi);
		camel_folder_change_info_add_uid (changes, camel_message_info_uid (mi));
		
		if ((mi->flags & CAMEL_IMAP_MESSAGE_RECENT))
			camel_folder_change_info_recent_uid(changes, camel_message_info_uid (mi));
	}
	g_ptr_array_free (messages, TRUE);
	
	/* Kludge around Microsoft Exchange 5.5 IMAP - See bug #5348 for details */
	if (camel_folder_summary_count (folder->summary) != exists) {
		CamelImapStore *imap_store = (CamelImapStore *) folder->parent_store;
		CamelImapResponse *response;
		
		/* forget the currently selected folder */
		if (imap_store->current_folder) {
			camel_object_unref (CAMEL_OBJECT (imap_store->current_folder));
			imap_store->current_folder = NULL;
		}
		
		/* now re-select it and process the EXISTS response */
		response = camel_imap_command (imap_store, folder, ex, NULL);
		camel_imap_response_free (imap_store, response);
	}
	
	return;
	
 lose:
	if (fetch_data) {
		for (i = 0; i < fetch_data->len; i++) {
			data = fetch_data->pdata[i];
			g_datalist_clear (&data);
		}
		g_ptr_array_free (fetch_data, TRUE);
	}
	if (messages) {
		for (i = 0; i < messages->len; i++) {
			if (messages->pdata[i])
				camel_folder_summary_info_free (folder->summary, messages->pdata[i]);
		}
		g_ptr_array_free (messages, TRUE);
	}
}

/* Called with the store's command_lock locked */
void
camel_imap_folder_changed (CamelFolder *folder, int exists,
			   GArray *expunged, CamelException *ex)
{
	CamelImapFolder *imap_folder = CAMEL_IMAP_FOLDER (folder);
	CamelFolderChangeInfo *changes;
	CamelMessageInfo *info;
	int len;
	
	CAMEL_IMAP_STORE_ASSERT_LOCKED (folder->parent_store, command_lock);
	
	changes = camel_folder_change_info_new ();
	if (expunged) {
		int i, id;
		
		for (i = 0; i < expunged->len; i++) {
			id = g_array_index (expunged, int, i);
			info = camel_folder_summary_index (folder->summary, id - 1);
			if (info == NULL) {
				/* FIXME: danw: does this mean that the summary is corrupt? */
				/* I guess a message that we never retrieved got expunged? */
				continue;
			}
			
			camel_folder_change_info_remove_uid (changes, camel_message_info_uid (info));
			CAMEL_IMAP_FOLDER_LOCK (imap_folder, cache_lock);
			camel_imap_message_cache_remove (imap_folder->cache, camel_message_info_uid (info));
			CAMEL_IMAP_FOLDER_UNLOCK (imap_folder, cache_lock);
			camel_folder_summary_remove (folder->summary, info);
			camel_folder_summary_info_free(folder->summary, info);
		}
	}
	
	len = camel_folder_summary_count (folder->summary);
	if (exists > len)
		imap_update_summary (folder, exists, changes, ex);
	
	if (camel_folder_change_info_changed (changes))
		camel_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed", changes);
	
	camel_folder_change_info_free (changes);
	camel_folder_summary_save (folder->summary);
}

static void
imap_thaw (CamelFolder *folder)
{
	CamelImapFolder *imap_folder;

	CAMEL_FOLDER_CLASS (disco_folder_class)->thaw (folder);
	if (camel_folder_is_frozen (folder))
		return;

	imap_folder = CAMEL_IMAP_FOLDER (folder);
	if (imap_folder->need_refresh) {
		imap_folder->need_refresh = FALSE;
		imap_refresh_info (folder, NULL);
	}
}


CamelStream *
camel_imap_folder_fetch_data (CamelImapFolder *imap_folder, const char *uid,
			      const char *section_text, gboolean cache_only,
			      CamelException *ex)
{
	CamelFolder *folder = CAMEL_FOLDER (imap_folder);
	CamelImapStore *store = CAMEL_IMAP_STORE (folder->parent_store);
	CamelImapResponse *response;
	CamelStream *stream;
	GData *fetch_data;
	char *found_uid;
	int i;
	
	/* EXPUNGE responses have to modify the cache, which means
	 * they have to grab the cache_lock while holding the
	 * command_lock. So we grab the command_lock now, in case
	 * we're going to need it below, since we can't grab it
	 * after the cache_lock.
	 */
	CAMEL_IMAP_STORE_LOCK (store, command_lock);
	
	CAMEL_IMAP_FOLDER_LOCK (imap_folder, cache_lock);
	stream = camel_imap_message_cache_get (imap_folder->cache, uid, section_text);
	if (!stream && (!strcmp (section_text, "HEADER") || !strcmp (section_text, "0")))
		stream = camel_imap_message_cache_get (imap_folder->cache, uid, "");
	if (stream || cache_only) {
		CAMEL_IMAP_FOLDER_UNLOCK (imap_folder, cache_lock);
		CAMEL_IMAP_STORE_UNLOCK (store, command_lock);
		return stream;
	}
	
	if (camel_disco_store_status (CAMEL_DISCO_STORE (store)) == CAMEL_DISCO_STORE_OFFLINE) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				     _("This message is not currently available"));
		CAMEL_IMAP_FOLDER_UNLOCK (imap_folder, cache_lock);
		CAMEL_IMAP_STORE_UNLOCK (store, command_lock);
		return NULL;
	}
	
	if (store->server_level < IMAP_LEVEL_IMAP4REV1 && !*section_text) {
		response = camel_imap_command (store, folder, ex,
					       "UID FETCH %s RFC822.PEEK",
					       uid);
	} else {
		response = camel_imap_command (store, folder, ex,
					       "UID FETCH %s BODY.PEEK[%s]",
					       uid, section_text);
	}
	/* We won't need the command_lock again after this. */
	CAMEL_IMAP_STORE_UNLOCK (store, command_lock);
	
	if (!response) {
		CAMEL_IMAP_FOLDER_UNLOCK (imap_folder, cache_lock);
		return NULL;
	}
	
	for (i = 0; i < response->untagged->len; i++) {
		fetch_data = parse_fetch_response (imap_folder, response->untagged->pdata[i]);
		found_uid = g_datalist_get_data (&fetch_data, "UID");
		stream = g_datalist_get_data (&fetch_data, "BODY_PART_STREAM");
		if (found_uid && stream && !strcmp (uid, found_uid))
			break;
		
		g_datalist_clear (&fetch_data);
		stream = NULL;
	}
	camel_imap_response_free (store, response);
	CAMEL_IMAP_FOLDER_UNLOCK (imap_folder, cache_lock);
	if (!stream) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      _("Could not find message body in FETCH "
					"response."));
	} else {
		camel_object_ref (CAMEL_OBJECT (stream));
		g_datalist_clear (&fetch_data);
	}
	
	return stream;
}

GData *
parse_fetch_response (CamelImapFolder *imap_folder, char *response)
{
	GData *data = NULL;
	char *start, *part_spec = NULL, *body = NULL, *uid = NULL;
	int body_len = 0;

	if (*response != '(') {
		long seq;

		if (*response != '*' || *(response + 1) != ' ')
			return NULL;
		seq = strtol (response + 2, &response, 10);
		if (seq == 0)
			return NULL;
		if (g_strncasecmp (response, " FETCH (", 8) != 0)
			return NULL;
		response += 7;

		g_datalist_set_data (&data, "SEQUENCE", GINT_TO_POINTER (seq));
	}

	do {
		/* Skip the initial '(' or the ' ' between elements */
		response++;

		if (!g_strncasecmp (response, "FLAGS ", 6)) {
			guint32 flags;

			response += 6;
			/* FIXME user flags */
			flags = imap_parse_flag_list (&response);

			g_datalist_set_data (&data, "FLAGS", GUINT_TO_POINTER (flags));
		} else if (!g_strncasecmp (response, "RFC822.SIZE ", 12)) {
			unsigned long size;

			response += 12;
			size = strtoul (response, &response, 10);
			g_datalist_set_data (&data, "RFC822.SIZE", GUINT_TO_POINTER (size));
		} else if (!g_strncasecmp (response, "BODY[", 5) ||
			   !g_strncasecmp (response, "RFC822 ", 7)) {
			char *p;

			if (*response == 'B') {
				response += 5;
				p = strchr (response, ']');
				if (!p || *(p + 1) != ' ')
					break;
				part_spec = g_strndup (response, p - response);
				response = p + 2;
			} else {
				part_spec = g_strdup ("");
				response += 7;
			}

			body = imap_parse_nstring (&response, &body_len);
			if (!response) {
				g_free (part_spec);
				break;
			}

			if (!body)
				body = g_strdup ("");
			g_datalist_set_data_full (&data, "BODY_PART_SPEC", part_spec, g_free);
			g_datalist_set_data_full (&data, "BODY_PART_DATA", body, g_free);
			g_datalist_set_data (&data, "BODY_PART_LEN", GINT_TO_POINTER (body_len));
		} else if (!g_strncasecmp (response, "BODY ", 5) ||
			   !g_strncasecmp (response, "BODYSTRUCTURE ", 14)) {
			response = strchr (response, ' ') + 1;
			start = response;
			imap_skip_list (&response);
			g_datalist_set_data_full (&data, "BODY", g_strndup (start, response - start), g_free);
		} else if (!g_strncasecmp (response, "UID ", 4)) {
			int len;

			len = strcspn (response + 4, " )");
			uid = g_strndup (response + 4, len);
			g_datalist_set_data_full (&data, "UID", uid, g_free);
			response += 4 + len;
		} else {
			g_warning ("Unexpected FETCH response from server: "
				   "(%s", response);
			break;
		}
	} while (response && *response != ')');

	if (!response || *response != ')') {
		g_datalist_clear (&data);
		return NULL;
	}

	if (uid && body) {
		CamelStream *stream;

		CAMEL_IMAP_FOLDER_LOCK (imap_folder, cache_lock);
		stream = camel_imap_message_cache_insert (imap_folder->cache,
							  uid, part_spec,
							  body, body_len, NULL);
		CAMEL_IMAP_FOLDER_UNLOCK (imap_folder, cache_lock);
		g_datalist_set_data_full (&data, "BODY_PART_STREAM", stream,
					  (GDestroyNotify)camel_object_unref);
	}

	return data;
}

