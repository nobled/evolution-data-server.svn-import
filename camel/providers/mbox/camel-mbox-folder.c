/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/* camel-mbox-folder.c : Abstract class for an email folder */

/* 
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com> 
 *          Michael Zucchi <notzed@helixcode.com>
 *
 * Copyright (C) 1999, 2000 Helix Code Inc.
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


#include <config.h> 

#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include "camel-mbox-folder.h"
#include "camel-mbox-store.h"
#include "string-utils.h"
#include "camel-stream-fs.h"
#include "camel-mbox-summary.h"
#include "camel-data-wrapper.h"
#include "camel-mime-message.h"
#include "camel-stream-filter.h"
#include "camel-mime-filter-from.h"
#include "camel-exception.h"

#define d(x)

static CamelFolderClass *parent_class=NULL;

/* Returns the class for a CamelMboxFolder */
#define CMBOXF_CLASS(so) CAMEL_MBOX_FOLDER_CLASS (GTK_OBJECT(so)->klass)
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (GTK_OBJECT(so)->klass)
#define CMBOXS_CLASS(so) CAMEL_STORE_CLASS (GTK_OBJECT(so)->klass)


static void mbox_init (CamelFolder *folder, CamelStore *parent_store,
		       CamelFolder *parent_folder, const gchar *name,
		       gchar *separator, gboolean path_begins_with_sep,
		       CamelException *ex);

static void mbox_sync (CamelFolder *folder, gboolean expunge, CamelException *ex);
static gint mbox_get_message_count (CamelFolder *folder, CamelException *ex);
static void mbox_append_message (CamelFolder *folder, CamelMimeMessage *message, CamelException *ex);
static GPtrArray *mbox_get_uids (CamelFolder *folder, CamelException *ex);
static GPtrArray *mbox_get_subfolder_names (CamelFolder *folder, CamelException *ex);
static GPtrArray *mbox_get_summary (CamelFolder *folder, CamelException *ex);
static void mbox_free_summary (CamelFolder *folder, GPtrArray *array);
static CamelMimeMessage *mbox_get_message_by_uid (CamelFolder *folder, const gchar *uid, CamelException *ex);

static void mbox_expunge (CamelFolder *folder, CamelException *ex);
#if 0
static void _copy_message_to (CamelFolder *folder, CamelMimeMessage *message, CamelFolder *dest_folder, CamelException *ex);
static const gchar *_get_message_uid (CamelFolder *folder, CamelMimeMessage *message, CamelException *ex);
#endif

static void mbox_delete_message_by_uid(CamelFolder *folder, const gchar *uid, CamelException *ex);

static const CamelMessageInfo *mbox_summary_get_by_uid(CamelFolder *f, const char *uid);

static GList *mbox_search_by_expression(CamelFolder *folder, const char *expression, CamelException *ex);

static guint32 mbox_get_message_flags (CamelFolder *folder, const char *uid, CamelException *ex);
static void mbox_set_message_flags (CamelFolder *folder, const char *uid, guint32 flags, guint32 set, CamelException *ex);
static gboolean mbox_get_message_user_flag (CamelFolder *folder, const char *uid, const char *name, CamelException *ex);
static void mbox_set_message_user_flag (CamelFolder *folder, const char *uid, const char *name, gboolean value, CamelException *ex);


static void mbox_finalize (GtkObject *object);

static void
camel_mbox_folder_class_init (CamelMboxFolderClass *camel_mbox_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS (camel_mbox_folder_class);
	GtkObjectClass *gtk_object_class = GTK_OBJECT_CLASS (camel_folder_class);

	parent_class = gtk_type_class (camel_folder_get_type ());
		
	/* virtual method definition */

	/* virtual method overload */
	camel_folder_class->init = mbox_init;
	camel_folder_class->sync = mbox_sync;
	camel_folder_class->get_message_count = mbox_get_message_count;
	camel_folder_class->append_message = mbox_append_message;
	camel_folder_class->get_uids = mbox_get_uids;
	camel_folder_class->get_subfolder_names = mbox_get_subfolder_names;
	camel_folder_class->get_summary = mbox_get_summary;
	camel_folder_class->free_summary = mbox_free_summary;
	camel_folder_class->expunge = mbox_expunge;

	camel_folder_class->get_message_by_uid = mbox_get_message_by_uid;
	camel_folder_class->delete_message_by_uid = mbox_delete_message_by_uid;

	camel_folder_class->search_by_expression = mbox_search_by_expression;

	camel_folder_class->summary_get_by_uid = mbox_summary_get_by_uid;

	camel_folder_class->get_message_flags = mbox_get_message_flags;
	camel_folder_class->set_message_flags = mbox_set_message_flags;
	camel_folder_class->get_message_user_flag = mbox_get_message_user_flag;
	camel_folder_class->set_message_user_flag = mbox_set_message_user_flag;

	gtk_object_class->finalize = mbox_finalize;
	
}

static void           
mbox_finalize (GtkObject *object)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER (object);

	g_free (mbox_folder->folder_file_path);
	g_free (mbox_folder->summary_file_path);
	g_free (mbox_folder->folder_dir_path);
	g_free (mbox_folder->index_file_path);

	GTK_OBJECT_CLASS (parent_class)->finalize (object);
}

GtkType
camel_mbox_folder_get_type (void)
{
	static GtkType camel_mbox_folder_type = 0;
	
	if (!camel_mbox_folder_type)	{
		GtkTypeInfo camel_mbox_folder_info =	
		{
			"CamelMboxFolder",
			sizeof (CamelMboxFolder),
			sizeof (CamelMboxFolderClass),
			(GtkClassInitFunc) camel_mbox_folder_class_init,
			(GtkObjectInitFunc) NULL,
				/* reserved_1 */ NULL,
				/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};
		
		camel_mbox_folder_type = gtk_type_unique (CAMEL_FOLDER_TYPE, &camel_mbox_folder_info);
	}
	
	return camel_mbox_folder_type;
}

static void 
mbox_init (CamelFolder *folder, CamelStore *parent_store,
       CamelFolder *parent_folder, const gchar *name, gchar *separator,
       gboolean path_begins_with_sep, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = (CamelMboxFolder *)folder;
	const gchar *root_dir_path;
	gchar *real_name;
	int forceindex;
	struct stat st;

	/* call parent method */
	parent_class->init (folder, parent_store, parent_folder,
			    name, separator, path_begins_with_sep, ex);
	if (camel_exception_get_id (ex))
		return;

	/* we assume that the parent init
	   method checks for the existance of @folder */
	folder->can_hold_messages = TRUE;
	folder->can_hold_folders = TRUE;
	folder->has_summary_capability = TRUE;
	folder->has_search_capability = TRUE;

	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED |
		CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT |
		CAMEL_MESSAGE_FLAGGED |
		CAMEL_MESSAGE_SEEN |
		CAMEL_MESSAGE_USER;
	/* FIXME: we don't actually preserve user flags right now. */

 	mbox_folder->summary = NULL;
 	mbox_folder->search = NULL;

	/* now set the name info */
	g_free (mbox_folder->folder_file_path);
	g_free (mbox_folder->folder_dir_path);
	g_free (mbox_folder->index_file_path);

	root_dir_path = camel_mbox_store_get_toplevel_dir (CAMEL_MBOX_STORE(folder->parent_store));

	real_name = g_basename (folder->full_name);
	mbox_folder->folder_file_path = g_strdup_printf ("%s/%s", root_dir_path, real_name);
	mbox_folder->summary_file_path = g_strdup_printf ("%s/%s-ev-summary", root_dir_path, real_name);
	mbox_folder->folder_dir_path = g_strdup_printf ("%s/%s.sdb", root_dir_path, real_name);
	mbox_folder->index_file_path = g_strdup_printf ("%s/%s.ibex", root_dir_path, real_name);

	/* if we have no index file, force it */
	forceindex = stat(mbox_folder->index_file_path, &st) == -1;

	printf("loading ibex\n");
	mbox_folder->index = ibex_open(mbox_folder->index_file_path, O_CREAT|O_RDWR, 0600);
	printf("loaded ibex\n");
	if (mbox_folder->index == NULL) {
		/* yes, this isn't fatal at all */
		g_warning("Could not open/create index file: %s: indexing not performed",
			  strerror(errno));
	}

	/* no summary (disk or memory), and we're proverbially screwed */
	printf("loading summary\n");
	mbox_folder->summary = camel_mbox_summary_new(mbox_folder->summary_file_path, mbox_folder->folder_file_path, mbox_folder->index);
	if (mbox_folder->summary == NULL
	    || camel_mbox_summary_load(mbox_folder->summary, forceindex) == -1) {
		camel_exception_set (ex, 
				     CAMEL_EXCEPTION_FOLDER_INVALID, /* FIXME: right error code */
				     "Could not create summary");
		return;
	}
	printf("summary loaded\n");
}

static void
mbox_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER (folder);

	if (expunge)
		mbox_expunge(folder, ex);
	else
		camel_mbox_summary_sync(mbox_folder->summary, FALSE);

	/* save index */
	if (mbox_folder->index) {
		ibex_close(mbox_folder->index);
		mbox_folder->index = NULL;
	}
	if (mbox_folder->summary) {
		camel_folder_summary_save ((CamelFolderSummary *)mbox_folder->summary);
		gtk_object_unref((GtkObject *)mbox_folder->summary);
		mbox_folder->summary = NULL;
	}
	if (mbox_folder->search) {
		gtk_object_unref((GtkObject *)mbox_folder->search);
		mbox_folder->search = NULL;
	}
}

static void
mbox_expunge (CamelFolder *folder, CamelException *ex)
{
	CamelMboxFolder *mbox = (CamelMboxFolder *)folder;

	if (camel_mbox_summary_sync(mbox->summary, TRUE) == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID, /* FIXME: right error code */
				      "Could not expunge: %s", strerror(errno));
	}

	/* TODO: check it actually changed */
	gtk_signal_emit_by_name((GtkObject *)folder, "folder_changed", 0);
}

static gint
mbox_get_message_count (CamelFolder *folder, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = (CamelMboxFolder *)folder;

	g_assert (folder);
	g_assert (mbox_folder->summary);
	
	return camel_folder_summary_count((CamelFolderSummary *)mbox_folder->summary);
}

/* FIXME: this may need some tweaking for performance? */
static void
mbox_append_message (CamelFolder *folder, CamelMimeMessage *message, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER (folder);
	CamelStream *output_stream = NULL, *filter_stream = NULL;
	CamelMimeFilter *filter_from = NULL;
	struct stat st;
	off_t seek;
	char *xev, last;
	guint32 uid;

	if (stat(mbox_folder->folder_file_path, &st) != 0)
		goto fail;

	output_stream = camel_stream_fs_new_with_name (mbox_folder->folder_file_path, O_RDWR, 0600);
	if (output_stream == NULL)
		goto fail;

	if (st.st_size) {
		seek = camel_seekable_stream_seek((CamelSeekableStream *)output_stream, st.st_size - 1, SEEK_SET);
		if (++seek != st.st_size)
			goto fail;

		/* If the mbox doesn't end with a newline, fix that. */
		if (camel_stream_read (output_stream, &last, 1) != 1)
			goto fail;
		if (last != '\n')
			camel_stream_write (output_stream, "\n", 1);
	} else
		seek = 0;

	/* assign a new x-evolution header/uid */
	camel_medium_remove_header((CamelMedium *)message, "X-Evolution");
	uid = camel_folder_summary_next_uid((CamelFolderSummary *)mbox_folder->summary);
	xev = g_strdup_printf("%08x-0000", uid);
	camel_medium_add_header((CamelMedium *)message, "X-Evolution", xev);
	g_free(xev);

	/* we must write this to the non-filtered stream ... */
	if (camel_stream_write_string (output_stream, "From - \n") == -1)
		goto fail;

	/* and write the content to the filtering stream, that translated '\nFrom' into '\n>From' */
	filter_stream = (CamelStream *)camel_stream_filter_new_with_stream(output_stream);
	filter_from = (CamelMimeFilter *)camel_mime_filter_from_new();
	camel_stream_filter_add((CamelStreamFilter *)filter_stream, filter_from);
	if (camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (message), filter_stream) == -1)
		goto fail;

	if (camel_stream_close (filter_stream) == -1)
		goto fail;

	/* filter stream ref's the output stream itself, so we need to unref it too */
	gtk_object_unref (GTK_OBJECT (filter_from));
	gtk_object_unref (GTK_OBJECT (filter_stream));
	gtk_object_unref (GTK_OBJECT (output_stream));

	/* force a summary update - will only update from the new position, if it can */
	camel_mbox_summary_update(mbox_folder->summary, seek);
	return;

fail:
	if (camel_exception_is_set (ex)) {
		camel_exception_setv (ex, camel_exception_get_id (ex),
				      "Cannot append message to mbox file: %s",
				      camel_exception_get_description (ex));
	} else {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      "Cannot append message to mbox file: %s",
				      g_strerror (errno));
	}
	if (filter_stream) {
		/*camel_stream_close (filter_stream);*/
		gtk_object_unref ((GtkObject *)filter_stream);
	}
	if (output_stream)
		gtk_object_unref ((GtkObject *)output_stream);

	if (filter_from)
		gtk_object_unref ((GtkObject *)filter_from);

	/* make sure the file isn't munged by us */
	if (seek != -1) {
		int fd = open(mbox_folder->folder_file_path, O_WRONLY, 0600);
		if (fd != -1) {
			ftruncate(fd, st.st_size);
			close(fd);
		}
	}
}

static GPtrArray *
mbox_get_uids (CamelFolder *folder, CamelException *ex) 
{
	GPtrArray *array;
	CamelMboxFolder *mbox_folder = (CamelMboxFolder *)folder;
	int i, count;

	count = camel_folder_summary_count((CamelFolderSummary *)mbox_folder->summary);
	array = g_ptr_array_new ();
	g_ptr_array_set_size (array, count);
	for (i=0;i<count;i++) {
		CamelMboxMessageInfo *info = (CamelMboxMessageInfo *)camel_folder_summary_index((CamelFolderSummary *)mbox_folder->summary, i);
		array->pdata[i] = g_strdup(info->info.uid);
	}
	
	return array;
}

static GPtrArray *
mbox_get_subfolder_names (CamelFolder *folder, CamelException *ex)
{
	/* No subfolders. */
	return g_ptr_array_new ();
}

static void
mbox_delete_message_by_uid(CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	CamelMessageInfo *info;
	CamelMboxFolder *mf = (CamelMboxFolder *)folder;

	info = camel_folder_summary_uid((CamelFolderSummary *)mf->summary, uid);
	if (info) {
		info->flags |=  CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_FOLDER_FLAGGED;
		camel_folder_summary_touch((CamelFolderSummary *)mf->summary);
	}
}

static CamelMimeMessage *
mbox_get_message_by_uid (CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = CAMEL_MBOX_FOLDER (folder);
	CamelStream *message_stream = NULL;
	CamelMimeMessage *message = NULL;
	CamelMboxMessageInfo *info;
	CamelMimeParser *parser = NULL;
	char *buffer;
	int len;

	/* get the message summary info */
	info = (CamelMboxMessageInfo *)camel_folder_summary_uid((CamelFolderSummary *)mbox_folder->summary, uid);

	if (info == NULL) {
		errno = ENOENT;
		goto fail;
	}

	/* if this has no content, its an error in the library */
	g_assert(info->info.content);
	g_assert(info->frompos != -1);

	/* where we read from */
	message_stream = camel_stream_fs_new_with_name (mbox_folder->folder_file_path, O_RDONLY, 0);
	if (message_stream == NULL)
		goto fail;

	/* we use a parser to verify the message is correct, and in the correct position */
	parser = camel_mime_parser_new();
	camel_mime_parser_init_with_stream(parser, message_stream);
	gtk_object_unref((GtkObject *)message_stream);
	camel_mime_parser_scan_from(parser, TRUE);

	camel_mime_parser_seek(parser, info->frompos, SEEK_SET);
	if (camel_mime_parser_step(parser, &buffer, &len) != HSCAN_FROM) {
		g_warning("File appears truncated");
		goto fail;
	}

	if (camel_mime_parser_tell_start_from(parser) != info->frompos) {
		g_warning("Summary doesn't match the folder contents!  eek!\n"
			  "  expecting offset %ld got %ld", (long int)info->frompos,
			  (long int)camel_mime_parser_tell_start_from(parser));
		errno = EINVAL;
		goto fail;
	}

	message = camel_mime_message_new();
	if (camel_mime_part_construct_from_parser((CamelMimePart *)message, parser) == -1) {
		g_warning("Construction failed");
		goto fail;
	}
	gtk_object_unref((GtkObject *)parser);

	return message;

fail:
	camel_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
			      "Cannot get message: %s",
			      g_strerror(errno));

	if (parser)
		gtk_object_unref((GtkObject *)parser);
	if (message)
		gtk_object_unref((GtkObject *)message);

	return NULL;
}

GPtrArray *
mbox_get_summary (CamelFolder *folder, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = (CamelMboxFolder *)folder;

	return ((CamelFolderSummary *)mbox_folder->summary)->messages;
}

void
mbox_free_summary (CamelFolder *folder, GPtrArray *array)
{
	/* no-op */
}

/* get a single message info, by uid */
static const CamelMessageInfo *
mbox_summary_get_by_uid(CamelFolder *f, const char *uid)
{
	CamelMboxFolder *mbox_folder = (CamelMboxFolder *)f;

	return camel_folder_summary_uid((CamelFolderSummary *)mbox_folder->summary, uid);
}

static GList *
mbox_search_by_expression(CamelFolder *folder, const char *expression, CamelException *ex)
{
	CamelMboxFolder *mbox_folder = (CamelMboxFolder *)folder;

	if (mbox_folder->search == NULL) {
		mbox_folder->search = camel_folder_search_new();
	}

	camel_folder_search_set_folder(mbox_folder->search, folder);
	if (mbox_folder->summary)
		/* FIXME: dont access summary array directly? */
		camel_folder_search_set_summary(mbox_folder->search, ((CamelFolderSummary *)mbox_folder->summary)->messages);
	camel_folder_search_set_body_index(mbox_folder->search, mbox_folder->index);

	return camel_folder_search_execute_expression(mbox_folder->search, expression, ex);
}

static guint32
mbox_get_message_flags (CamelFolder *folder, const char *uid,
			CamelException *ex)
{
	CamelMessageInfo *info;
	CamelMboxFolder *mf = (CamelMboxFolder *)folder;

	info = camel_folder_summary_uid((CamelFolderSummary *)mf->summary, uid);
	if (info)
		return info->flags;
	else {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				     "No such message %s in %s.", uid,
				     folder->name);
		return 0;
	}
}

static void
mbox_set_message_flags (CamelFolder *folder, const char *uid, guint32 flags,
			guint32 set, CamelException *ex)
{
	CamelMessageInfo *info;
	CamelMboxFolder *mf = (CamelMboxFolder *)folder;

	info = camel_folder_summary_uid((CamelFolderSummary *)mf->summary, uid);
	if (info) {
		info->flags = (info->flags & ~flags) | (set & flags) |
			CAMEL_MESSAGE_FOLDER_FLAGGED;
		camel_folder_summary_touch((CamelFolderSummary *)mf->summary);
		gtk_signal_emit_by_name((GtkObject *)folder, "message_changed", uid);
	} else {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				     "No such message %s in %s.", uid,
				     folder->name);
	}
}

static gboolean
mbox_get_message_user_flag (CamelFolder *folder, const char *uid,
			    const char *name, CamelException *ex)
{
	CamelMessageInfo *info;
	CamelMboxFolder *mf = (CamelMboxFolder *)folder;

	info = camel_folder_summary_uid((CamelFolderSummary *)mf->summary, uid);
	if (info)
		return camel_flag_get(&info->user_flags, name);
	else {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				     "No such message %s in %s.", uid,
				     folder->name);
		return FALSE;
	}
}

static void mbox_set_message_user_flag (CamelFolder *folder, const char *uid,
					const char *name, gboolean value,
					CamelException *ex)
{
	CamelMessageInfo *info;
	CamelMboxFolder *mf = (CamelMboxFolder *)folder;

	info = camel_folder_summary_uid((CamelFolderSummary *)mf->summary, uid);
	if (info) {
		camel_flag_set(&info->user_flags, name, value);
		info->flags |= CAMEL_MESSAGE_FOLDER_FLAGGED;
		camel_folder_summary_touch((CamelFolderSummary *)mf->summary);
		gtk_signal_emit_by_name((GtkObject *)folder, "message_changed", uid);
	} else {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				     "No such message %s in %s.", uid,
				     folder->name);
	}
}
