/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *           Jeffrey Stedfast <fejj@ximian.com>
 *
 *  This program is free software; you can redistribute it and/or 
 *  modify it under the terms of the GNU General Public License as 
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "camel-exception.h"
#include "camel-vee-folder.h"
#include "camel-store.h"
#include "camel-folder-summary.h"
#include "camel-mime-message.h"
#include "camel-folder-search.h"

#include "camel-vee-store.h"	/* for open flags */
#include "camel-private.h"

#include "e-util/md5-utils.h"

#if defined (DOEPOOLV) || defined (DOESTRV)
#include "e-util/e-memory.h"
#endif

#define d(x)

#define _PRIVATE(o) (((CamelVeeFolder *)(o))->priv)

static void vee_refresh_info(CamelFolder *folder, CamelException *ex);

static void vee_sync (CamelFolder *folder, gboolean expunge, CamelException *ex);
static void vee_expunge (CamelFolder *folder, CamelException *ex);

static CamelMimeMessage *vee_get_message (CamelFolder *folder, const gchar *uid, CamelException *ex);
static void vee_move_messages_to(CamelFolder *source, GPtrArray *uids, CamelFolder *dest, CamelException *ex);

static GPtrArray *vee_search_by_expression(CamelFolder *folder, const char *expression, CamelException *ex);

static void vee_set_message_flags (CamelFolder *folder, const char *uid, guint32 flags, guint32 set);
static void vee_set_message_user_flag (CamelFolder *folder, const char *uid, const char *name, gboolean value);

static void camel_vee_folder_class_init (CamelVeeFolderClass *klass);
static void camel_vee_folder_init       (CamelVeeFolder *obj);
static void camel_vee_folder_finalise   (CamelObject *obj);

static int vee_folder_build_folder(CamelVeeFolder *vf, CamelFolder *source, CamelException *ex);
static void vee_folder_remove_folder(CamelVeeFolder *vf, CamelFolder *source);

static void message_changed(CamelFolder *f, const char *uid, CamelVeeFolder *vf);
static void folder_changed(CamelFolder *sub, CamelFolderChangeInfo *changes, CamelVeeFolder *vf);

static CamelFolderClass *camel_vee_folder_parent;

/* a vfolder for unmatched messages */
/* use folder_unmatched->summary_lock for access to unmatched_uids or appropriate internals, for consistency */
static CamelVeeFolder *folder_unmatched;
static GHashTable *unmatched_uids; /* a refcount of uid's that are matched by any rules */
#ifdef ENABLE_THREADS
#include <pthread.h>
static pthread_mutex_t unmatched_lock = PTHREAD_MUTEX_INITIALIZER;
/* only used to initialise folder_unmatched */
#define UNMATCHED_LOCK() pthread_mutex_lock(&unmatched_lock)
#define UNMATCHED_UNLOCK() pthread_mutex_unlock(&unmatched_lock)
#else
#define UNMATCHED_LOCK()
#define UNMATCHED_UNLOCK()
#endif

CamelType
camel_vee_folder_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_folder_get_type (), "CamelVeeFolder",
					    sizeof (CamelVeeFolder),
					    sizeof (CamelVeeFolderClass),
					    (CamelObjectClassInitFunc) camel_vee_folder_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_vee_folder_init,
					    (CamelObjectFinalizeFunc) camel_vee_folder_finalise);
	}
	
	return type;
}

static void
camel_vee_folder_class_init (CamelVeeFolderClass *klass)
{
	CamelFolderClass *folder_class = (CamelFolderClass *) klass;

	camel_vee_folder_parent = CAMEL_FOLDER_CLASS(camel_type_get_global_classfuncs (camel_folder_get_type ()));

	folder_class->refresh_info = vee_refresh_info;
	folder_class->sync = vee_sync;
	folder_class->expunge = vee_expunge;

	folder_class->get_message = vee_get_message;
	folder_class->move_messages_to = vee_move_messages_to;

	folder_class->search_by_expression = vee_search_by_expression;

	folder_class->set_message_flags = vee_set_message_flags;
	folder_class->set_message_user_flag = vee_set_message_user_flag;
}

static void
camel_vee_folder_init (CamelVeeFolder *obj)
{
	struct _CamelVeeFolderPrivate *p;
	CamelFolder *folder = (CamelFolder *)obj;

	p = _PRIVATE(obj) = g_malloc0(sizeof(*p));

	folder->has_summary_capability = TRUE;
	folder->has_search_capability = TRUE;

	/* FIXME: what to do about user flags if the subfolder doesn't support them? */
	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED |
		CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT |
		CAMEL_MESSAGE_FLAGGED |
		CAMEL_MESSAGE_SEEN;

	obj->changes = camel_folder_change_info_new();
	obj->search = camel_folder_search_new();

#ifdef ENABLE_THREADS
	p->summary_lock = g_mutex_new();
	p->subfolder_lock = g_mutex_new();
	p->changed_lock = g_mutex_new();
#endif

}

static void
camel_vee_folder_finalise (CamelObject *obj)
{
	CamelVeeFolder *vf = (CamelVeeFolder *)obj;
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	GList *node;

	/* FIXME: check leaks */
	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;

		if (vf != folder_unmatched) {
			camel_object_unhook_event((CamelObject *)f, "folder_changed", (CamelObjectEventHookFunc) folder_changed, vf);
			camel_object_unhook_event((CamelObject *)f, "message_changed", (CamelObjectEventHookFunc) message_changed, vf);
			/* this updates the vfolder */
			if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0)
				vee_folder_remove_folder(vf, f);
		}
		camel_object_unref((CamelObject *)f);

		node = g_list_next(node);
	}

	g_free(vf->expression);
	g_free(vf->vname);
	
	g_list_free(p->folders);
	g_list_free(p->folders_changed);

	camel_folder_change_info_free(vf->changes);
	camel_object_unref((CamelObject *)vf->search);

#ifdef ENABLE_THREADS
	g_mutex_free(p->summary_lock);
	g_mutex_free(p->subfolder_lock);
	g_mutex_free(p->changed_lock);
#endif
	g_free(p);
}

static void
vee_folder_construct (CamelVeeFolder *vf, CamelStore *parent_store, const char *name, guint32 flags)
{
	CamelFolder *folder = (CamelFolder *)vf;
	char *tmp;
	
	vf->flags = flags;
	vf->vname = g_strdup(name);
	tmp = strrchr(vf->vname, '/');
	if (tmp)
		tmp++;
	else
		tmp = vf->vname;
	camel_folder_construct(folder, parent_store, vf->vname, tmp);

	/* should CamelVeeMessageInfo be subclassable ..? */
	folder->summary = camel_folder_summary_new();
	folder->summary->message_info_size = sizeof(CamelVeeMessageInfo);
}

void
camel_vee_folder_construct(CamelVeeFolder *vf, CamelStore *parent_store, const char *name, guint32 flags)
{
	UNMATCHED_LOCK();
	
	/* setup unmatched folder if we haven't yet */
	if (folder_unmatched == NULL) {
		unmatched_uids = g_hash_table_new (g_str_hash, g_str_equal);
		folder_unmatched = (CamelVeeFolder *)camel_object_new (camel_vee_folder_get_type ());
		d(printf("created foldeer unmatched %p\n", folder_unmatched));
		
		vee_folder_construct (folder_unmatched, parent_store, CAMEL_UNMATCHED_NAME, CAMEL_STORE_FOLDER_PRIVATE);
	}
	
	UNMATCHED_UNLOCK();
	
	vee_folder_construct (vf, parent_store, name, flags);
}

/**
 * camel_vee_folder_new:
 * @parent_store: the parent CamelVeeStore
 * @name: the vfolder name
 * @ex: a CamelException
 *
 * Create a new CamelVeeFolder object.
 *
 * Return value: A new CamelVeeFolder widget.
 **/
CamelFolder *
camel_vee_folder_new(CamelStore *parent_store, const char *name, guint32 flags)
{
	CamelVeeFolder *vf;

	UNMATCHED_LOCK();

	/* setup unmatched folder if we haven't yet */
	if (folder_unmatched == NULL) {
		unmatched_uids = g_hash_table_new(g_str_hash, g_str_equal);
		folder_unmatched = vf = (CamelVeeFolder *)camel_object_new(camel_vee_folder_get_type());
		d(printf("created foldeer unmatched %p\n", folder_unmatched));
		vee_folder_construct (vf, parent_store, CAMEL_UNMATCHED_NAME, CAMEL_STORE_FOLDER_PRIVATE);
	}

	UNMATCHED_UNLOCK();

	if (strcmp(name, CAMEL_UNMATCHED_NAME) == 0) {
		camel_object_ref((CamelObject *)folder_unmatched);
		d(printf("returning unmatched %p, count = %d\n", folder_unmatched, camel_folder_get_message_count((CamelFolder *)folder_unmatched)));
		return (CamelFolder *)folder_unmatched;
	}

	vf = (CamelVeeFolder *)camel_object_new(camel_vee_folder_get_type());
	vee_folder_construct(vf, parent_store, name, flags);

	d(printf("returning folder %s %p, count = %d\n", name, vf, camel_folder_get_message_count((CamelFolder *)vf)));

	return (CamelFolder *)vf;
}

void
camel_vee_folder_set_expression(CamelVeeFolder *vf, const char *query)
{
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	GList *node;

	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);

	/* no change, do nothing */
	if ((vf->expression && query && strcmp(vf->expression, query) == 0)
	    || (vf->expression == NULL && query == NULL)) {
		CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);
		return;
	}

	g_free(vf->expression);
	if (query)
		vf->expression = g_strdup(query);

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;

		if (vee_folder_build_folder(vf, f, NULL) == -1)
			break;

		node = node->next;
	}

	CAMEL_VEE_FOLDER_LOCK(vf, changed_lock);
	g_list_free(p->folders_changed);
	p->folders_changed = NULL;
	CAMEL_VEE_FOLDER_UNLOCK(vf, changed_lock);

	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);
}

/**
 * camel_vee_folder_add_folder:
 * @vf: Virtual Folder object
 * @sub: source CamelFolder to add to @vf
 *
 * Adds @sub as a source folder to @vf.
 **/
void
camel_vee_folder_add_folder(CamelVeeFolder *vf, CamelFolder *sub)
{
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf), *up = _PRIVATE(folder_unmatched);

	if (vf == (CamelVeeFolder *)sub) {
		g_warning("Adding a virtual folder to itself as source, ignored");
		return;
	}

	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);

	/* for normal vfolders we want only unique ones, for unmatched we want them all recorded */
	if (g_list_find(p->folders, sub) == NULL) {
		camel_object_ref((CamelObject *)sub);
		p->folders = g_list_append(p->folders, sub);
	}
	if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0) {
		camel_object_ref((CamelObject *)sub);
		up->folders = g_list_append(up->folders, sub);
	}

	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);

	d(printf("camel_vee_folder_add_folde(%p, %p)\n", vf, sub));

	camel_object_hook_event((CamelObject *)sub, "folder_changed", (CamelObjectEventHookFunc)folder_changed, vf);
	camel_object_hook_event((CamelObject *)sub, "message_changed", (CamelObjectEventHookFunc)message_changed, vf);

	vee_folder_build_folder(vf, sub, NULL);
}

/**
 * camel_vee_folder_remove_folder:
 * @vf: Virtual Folder object
 * @sub: source CamelFolder to remove from @vf
 *
 * Removed the source folder, @sub, from the virtual folder, @vf.
 **/
void
camel_vee_folder_remove_folder(CamelVeeFolder *vf, CamelFolder *sub)
{
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf), *up = _PRIVATE(folder_unmatched);

	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);

	CAMEL_VEE_FOLDER_LOCK(vf, changed_lock);
	p->folders_changed = g_list_remove(p->folders_changed, sub);
	CAMEL_VEE_FOLDER_UNLOCK(vf, changed_lock);

	if (g_list_find(p->folders, sub) == NULL) {
		CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);
		return;
	}

	p->folders = g_list_remove(p->folders, sub);
	if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0 && g_list_find(p->folders, sub) != NULL) {
		up->folders = g_list_remove(up->folders, sub);
		camel_object_unref((CamelObject *)sub);
	}

	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);

	vee_folder_remove_folder(vf, sub);

	camel_object_unref((CamelObject *)sub);
}

static void
remove_folders(CamelFolder *folder, CamelFolder *foldercopy, CamelVeeFolder *vf)
{
	camel_vee_folder_remove_folder(vf, folder);
	camel_object_unref((CamelObject *)folder);
}

/**
 * camel_vee_folder_set_folders:
 * @vf: 
 * @folders: 
 * 
 * Set the whole list of folder sources on a vee folder.
 **/
void
camel_vee_folder_set_folders(CamelVeeFolder *vf, GList *folders)
{
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	GHashTable *remove = g_hash_table_new(NULL, NULL);
	GList *l;
	CamelFolder *folder;
	int changed;

	/* setup a table of all folders we have currently */
	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);
	l = p->folders;
	while (l) {
		g_hash_table_insert(remove, l->data, l->data);
		camel_object_ref((CamelObject *)l->data);
		l = l->next;
	}
	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);

	/* if we already have the folder, ignore it, otherwise add it */
	l = folders;
	while (l) {
		if ((folder = g_hash_table_lookup(remove, l->data))) {
			g_hash_table_remove(remove, folder);
			camel_object_unref((CamelObject *)folder);

			/* if this was a changed folder, re-update it while we're here */
			CAMEL_VEE_FOLDER_LOCK(vf, changed_lock);
			changed = g_list_find(p->folders_changed, folder) != NULL;
			if (changed)
				p->folders_changed = g_list_remove(p->folders_changed, folder);
			CAMEL_VEE_FOLDER_UNLOCK(vf, changed_lock);
			if (changed)
				vee_folder_build_folder(vf, folder, NULL);
		} else {
			camel_vee_folder_add_folder(vf, l->data);
		}
		l = l->next;
	}

	/* then remove any we still have */
	g_hash_table_foreach(remove, (GHFunc)remove_folders, vf);
	g_hash_table_destroy(remove);
}

/**
 * camel_vee_folder_hash_folder:
 * @folder: 
 * @: 
 * 
 * Create a hash string representing the folder name, which should be
 * unique, and remain static for a given folder.
 **/
void
camel_vee_folder_hash_folder(CamelFolder *folder, char buffer[8])
{
	MD5Context ctx;
	unsigned char digest[16];
	unsigned int state = 0, save = 0;
	char *tmp;
	int i;

	md5_init(&ctx);
	tmp = camel_service_get_url((CamelService *)folder->parent_store);
	md5_update(&ctx, tmp, strlen(tmp));
	g_free(tmp);
	md5_update(&ctx, folder->full_name, strlen(folder->full_name));
	md5_final(&ctx, digest);
	base64_encode_close(digest, 6, FALSE, buffer, &state, &save);

	for (i=0;i<8;i++) {
		if (buffer[i] == '+')
			buffer[i] = '.';
		if (buffer[i] == '/')
			buffer[i] = '_';
	}
}

static void vee_refresh_info(CamelFolder *folder, CamelException *ex)
{
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	GList *node, *list;

	CAMEL_VEE_FOLDER_LOCK(vf, changed_lock);
	list = p->folders_changed;
	p->folders_changed = NULL;
	CAMEL_VEE_FOLDER_UNLOCK(vf, changed_lock);

	node = list;
	while (node) {
		CamelFolder *f = node->data;

		if (vee_folder_build_folder(vf, f, ex) == -1)
			break;

		node = node->next;
	}

	g_list_free(list);
}

static void
vee_sync(CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	GList *node;

	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;

		camel_folder_sync(f, expunge, ex);
		if (camel_exception_is_set(ex))
			break;

		if (vee_folder_build_folder(vf, f, ex) == -1)
			break;

		node = node->next;
	}

	CAMEL_VEE_FOLDER_LOCK(vf, changed_lock);
	g_list_free(p->folders_changed);
	p->folders_changed = NULL;
	CAMEL_VEE_FOLDER_UNLOCK(vf, changed_lock);

	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);
}

static void
vee_expunge (CamelFolder *folder, CamelException *ex)
{
	((CamelFolderClass *)((CamelObject *)folder)->classfuncs)->sync(folder, TRUE, ex);
}

static CamelMimeMessage *
vee_get_message(CamelFolder *folder, const char *uid, CamelException *ex)
{
	CamelVeeMessageInfo *mi;
	CamelMimeMessage *msg = NULL;

	mi = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, uid);
	if (mi) {
		msg =  camel_folder_get_message(mi->folder, camel_message_info_uid(mi)+8, ex);
		camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)mi);
	} else {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				     _("No such message %s in %s"), uid,
				     folder->name);
	}

	return msg;
}

static GPtrArray *
vee_search_by_expression(CamelFolder *folder, const char *expression, CamelException *ex)
{
	GList *node;
	GPtrArray *matches, *result = g_ptr_array_new ();
	char *expr;
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	GHashTable *searched = g_hash_table_new(NULL, NULL);

	CAMEL_VEE_FOLDER_LOCK(vf, subfolder_lock);

	expr = g_strdup_printf("(and %s %s)", vf->expression, expression);
	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;
		int i;
		char hash[8];
		
		/* make sure we only search each folder once - for unmatched folder to work right */
		if (g_hash_table_lookup(searched, f) == NULL) {
			camel_vee_folder_hash_folder(f, hash);
			matches = camel_folder_search_by_expression(f, expression, ex);
			for (i = 0; i < matches->len; i++) {
				char *uid = matches->pdata[i];
				g_ptr_array_add(result, g_strdup_printf("%.8s%s", hash, uid));
			}
			camel_folder_search_free(f, matches);
			g_hash_table_insert(searched, f, f);
		}
		node = g_list_next(node);
	}

	g_free(expr);
	CAMEL_VEE_FOLDER_UNLOCK(vf, subfolder_lock);

	g_hash_table_destroy(searched);

	return result;
}

static void
vee_set_message_flags(CamelFolder *folder, const char *uid, guint32 flags, guint32 set)
{
	CamelVeeMessageInfo *mi;

	mi = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, uid);
	if (mi) {
		((CamelFolderClass *)camel_vee_folder_parent)->set_message_flags(folder, uid, flags, set);
		camel_folder_set_message_flags(mi->folder, camel_message_info_uid(mi) + 8, flags, set);
		camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)mi);
	}
}

static void
vee_set_message_user_flag(CamelFolder *folder, const char *uid, const char *name, gboolean value)
{
	CamelVeeMessageInfo *mi;

	mi = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, uid);
	if (mi) {
		((CamelFolderClass *)camel_vee_folder_parent)->set_message_user_flag(folder, uid, name, value);
		camel_folder_set_message_user_flag(mi->folder, camel_message_info_uid(mi) + 8, name, value);
		camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)mi);
	}
}

static void
vee_move_messages_to (CamelFolder *folder, GPtrArray *uids, CamelFolder *dest, CamelException *ex)
{
	CamelVeeMessageInfo *mi;
	int i;
	
	for (i = 0; i < uids->len && !camel_exception_is_set (ex); i++) {
		mi = (CamelVeeMessageInfo *) camel_folder_summary_uid (folder->summary, uids->pdata[i]);
		if (mi) {
			/* noop if it we're moving from the same vfolder (uh, which should't happen but who knows) */
			if (folder != mi->folder) {
				GPtrArray *uids;
				
				uids = g_ptr_array_new ();
				g_ptr_array_add (uids, (char *) (camel_message_info_uid (mi) + 8));
				camel_folder_move_messages_to (mi->folder, uids, dest, ex);
				g_ptr_array_free (uids, TRUE);
			}
			camel_folder_summary_info_free (folder->summary, (CamelMessageInfo *)mi);
		} else {
			camel_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
					      _("No such message: %s"), uids->pdata[i]);
		}
	}
}

/* ********************************************************************** *
   utility functions */

/* must be called with summary_lock held */
static CamelVeeMessageInfo *
vee_folder_add_info(CamelVeeFolder *vf, CamelFolder *f, CamelMessageInfo *info, const char hash[8])
{
	CamelVeeMessageInfo *mi;
	char *uid;
	CamelFolder *folder = (CamelFolder *)vf;
	CamelMessageInfo *dinfo;

	uid = g_strdup_printf("%.8s%s", hash, camel_message_info_uid(info));
	dinfo = camel_folder_summary_uid(folder->summary, uid);
	if (dinfo) {
		d(printf("w:clash, we already have '%s' in summary\n", uid));
		g_free(uid);
		camel_folder_summary_info_free(folder->summary, dinfo);
		return NULL;
	}

	d(printf("adding uid %s to %s\n", uid, vf->vname));

	mi = (CamelVeeMessageInfo *)camel_folder_summary_info_new(folder->summary);
	camel_message_info_dup_to(info, (CamelMessageInfo *)mi);
#ifdef DOEPOOLV
	mi->info.strings = e_poolv_set(mi->info.strings, CAMEL_MESSAGE_INFO_UID, uid, TRUE);
#elif defined (DOESTRV)
	mi->info.strings = e_strv_set_ref_free(mi->info.strings, CAMEL_MESSAGE_INFO_UID, uid);
	mi->info.strings = e_strv_pack(mi->info.strings);
#else	
	g_free(mi->info.uid);
	mi->info.uid = uid;
#endif
	mi->folder = f;
	camel_folder_summary_add(folder->summary, (CamelMessageInfo *)mi);

	return mi;
}

/* must be called with summary_lock held */
static CamelVeeMessageInfo *
vee_folder_add_uid(CamelVeeFolder *vf, CamelFolder *f, const char *inuid, const char hash[8])
{
	CamelMessageInfo *info;
	CamelVeeMessageInfo *mi = NULL;

	info = camel_folder_get_message_info(f, inuid);
	if (info) {
		mi = vee_folder_add_info(vf, f, info, hash);
		camel_folder_free_message_info(f, info);
	}
	return mi;
}

static void
vee_folder_remove_folder(CamelVeeFolder *vf, CamelFolder *source)
{
	int i, count, n, still;
	char *oldkey;
	CamelFolder *folder = (CamelFolder *)vf;
	char hash[8];
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	CamelFolderChangeInfo *vf_changes = NULL, *unmatched_changes = NULL;

	if (vf == folder_unmatched)
		return;

	/* check if this folder is still to be part of unmatched */
	if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0) {
		CAMEL_VEE_FOLDER_LOCK(folder_unmatched, subfolder_lock);
		still = g_list_find(p->folders, source) != NULL;
		CAMEL_VEE_FOLDER_UNLOCK(folder_unmatched, subfolder_lock);
		camel_vee_folder_hash_folder(source, hash);
	} else {
		still = FALSE;
	}

	CAMEL_VEE_FOLDER_LOCK(vf, summary_lock);
	CAMEL_VEE_FOLDER_LOCK(folder_unmatched, summary_lock);

	count = camel_folder_summary_count(folder->summary);
	for (i=0;i<count;i++) {
		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_index(folder->summary, i);
		if (mi) {
			if (mi->folder == source) {
				const char *uid = camel_message_info_uid(mi);

				camel_folder_change_info_remove_uid(vf->changes, uid);
				camel_folder_summary_remove_index(folder->summary, i);
				i--;
				if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0) {
					if (still) {
						if (g_hash_table_lookup_extended(unmatched_uids, uid, (void **)&oldkey, (void **)&n)) {
							if (n == 1) {
								g_hash_table_remove(unmatched_uids, oldkey);
								if (vee_folder_add_uid(folder_unmatched, source, oldkey+8, hash))
									camel_folder_change_info_add_uid(folder_unmatched->changes, oldkey);
								g_free(oldkey);
							} else {
								g_hash_table_insert(unmatched_uids, oldkey, (void *)(n-1));
							}
						}
					} else {
						if (g_hash_table_lookup_extended(unmatched_uids, camel_message_info_uid(mi), (void **)&oldkey, (void **)&n)) {
							g_hash_table_remove(unmatched_uids, oldkey);
							g_free(oldkey);
						}
						camel_folder_summary_remove_uid(((CamelFolder *)folder_unmatched)->summary, uid);
					}
				}
			}
			camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)mi);
		}
	}

	if (camel_folder_change_info_changed(folder_unmatched->changes)) {
		unmatched_changes = folder_unmatched->changes;
		folder_unmatched->changes = camel_folder_change_info_new();
	}

	if (camel_folder_change_info_changed(vf->changes)) {
		vf_changes = vf->changes;
		vf->changes = camel_folder_change_info_new();
	}

	CAMEL_VEE_FOLDER_UNLOCK(folder_unmatched, summary_lock);
	CAMEL_VEE_FOLDER_UNLOCK(vf, summary_lock);

	if (unmatched_changes) {
		camel_object_trigger_event((CamelObject *)folder_unmatched, "folder_changed", unmatched_changes);
		camel_folder_change_info_free(unmatched_changes);
	}

	if (vf_changes) {
		camel_object_trigger_event((CamelObject *)vf, "folder_changed", vf_changes);
		camel_folder_change_info_free(vf_changes);
	}
}

struct _update_data {
	CamelFolder *source;
	CamelVeeFolder *vf;
	char hash[8];
};

static void
unmatched_check_uid(char *uidin, void *value, struct _update_data *u)
{
	char *uid;
	int n;

	uid = alloca(strlen(uidin)+9);
	sprintf(uid, "%.8s%s", u->hash, uidin);
	n = (int)g_hash_table_lookup(unmatched_uids, uid);
	if (n == 0) {
		if (vee_folder_add_uid(folder_unmatched, u->source, uidin, u->hash))
			camel_folder_change_info_add_uid(folder_unmatched->changes, uid);
	} else {
		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_uid(((CamelFolder *)folder_unmatched)->summary, uid);
		if (mi) {
			camel_folder_summary_remove(((CamelFolder *)folder_unmatched)->summary, (CamelMessageInfo *)mi);
			camel_folder_change_info_remove_uid(folder_unmatched->changes, uid);
			camel_folder_summary_info_free(((CamelFolder *)folder_unmatched)->summary, (CamelMessageInfo *)mi);
		}
	}
}

static void
folder_added_uid(char *uidin, void *value, struct _update_data *u)
{
	CamelVeeMessageInfo *mi;
	char *oldkey;
	int n;

	if ( (mi = vee_folder_add_uid(u->vf, u->source, uidin, u->hash)) ) {
		camel_folder_change_info_add_uid(u->vf->changes, camel_message_info_uid(mi));

		if (g_hash_table_lookup_extended(unmatched_uids, camel_message_info_uid(mi), (void **)&oldkey, (void **)&n)) {
			g_hash_table_insert(unmatched_uids, oldkey, (void *)(n+1));
		} else {
			g_hash_table_insert(unmatched_uids, g_strdup(camel_message_info_uid(mi)), (void *)1);
		}
	}
}

/* build query contents for a single folder */
static int
vee_folder_build_folder(CamelVeeFolder *vf, CamelFolder *source, CamelException *ex)
{
	GPtrArray *match, *all;
	GHashTable *allhash, *matchhash;
	CamelFolder *f = source;
	CamelFolder *folder = (CamelFolder *)vf;
	int i, n, count;
	struct _update_data u;
	CamelFolderChangeInfo *vf_changes = NULL, *unmatched_changes = NULL;

	if (vf == folder_unmatched)
		return 0;

	/* if we have no expression, or its been cleared, then act as if no matches */
	if (vf->expression == NULL) {
		match = g_ptr_array_new();
	} else {
		match = camel_folder_search_by_expression(f, vf->expression, ex);
		if (match == NULL)
			return -1;
	}

	u.source = source;
	u.vf = vf;
	camel_vee_folder_hash_folder(source, u.hash);

	CAMEL_VEE_FOLDER_LOCK(vf, summary_lock);

	/* we build 2 hash tables, one for all uid's not matched, the other for all matched uid's,
	   we just ref the real memory */
	matchhash = g_hash_table_new(g_str_hash, g_str_equal);
	for (i=0;i<match->len;i++)
		g_hash_table_insert(matchhash, match->pdata[i], (void *)1);

	allhash = g_hash_table_new(g_str_hash, g_str_equal);
	all = camel_folder_get_uids(f);
	for (i=0;i<all->len;i++)
		if (g_hash_table_lookup(matchhash, all->pdata[i]) == NULL)
			g_hash_table_insert(allhash, all->pdata[i], (void *)1);

	CAMEL_VEE_FOLDER_LOCK(folder_unmatched, summary_lock);

	/* scan, looking for "old" uid's to be removed */
	count = camel_folder_summary_count(folder->summary);
	for (i=0;i<count;i++) {
		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_index(folder->summary, i);

		if (mi) {
			if (mi->folder == source) {
				char *uid = (char *)camel_message_info_uid(mi), *oldkey;

				if (g_hash_table_lookup(matchhash, uid+8) == NULL) {
					camel_folder_summary_remove_index(folder->summary, i);
					camel_folder_change_info_remove_uid(vf->changes, camel_message_info_uid(mi));
					i--;
					if (g_hash_table_lookup_extended(unmatched_uids, uid, (void **)&oldkey, (void **)&n)) {
						if (n == 1) {
							g_hash_table_remove(unmatched_uids, oldkey);
							g_free(oldkey);
						} else {
							g_hash_table_insert(unmatched_uids, oldkey, (void *)(n-1));
						}
					}
				} else {
					g_hash_table_remove(matchhash, uid+8);
				}
			}
			camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)mi);
		}
	}

	/* now matchhash contains any new uid's, add them, etc */
	g_hash_table_foreach(matchhash, (GHFunc)folder_added_uid, &u);

	/* scan unmatched, remove any that have vanished, etc */
	count = camel_folder_summary_count(((CamelFolder *)folder_unmatched)->summary);
	for (i=0;i<count;i++) {
		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)camel_folder_summary_index(((CamelFolder *)folder_unmatched)->summary, i);

		if (mi) {
			if (mi->folder == source) {
				char *uid = (char *)camel_message_info_uid(mi);

				if (g_hash_table_lookup(allhash, uid+8) == NULL) {
					/* no longer exists at all, just remove it entirely */
					camel_folder_summary_remove_index(((CamelFolder *)folder_unmatched)->summary, i);
					camel_folder_change_info_remove_uid(folder_unmatched->changes, camel_message_info_uid(mi));
					i--;
				} else {
					g_hash_table_remove(allhash, uid+8);
				}
			}
			camel_folder_summary_info_free(((CamelFolder *)folder_unmatched)->summary, (CamelMessageInfo *)mi);
		}
	}

	/* now allhash contains all potentially new uid's for the unmatched folder, process */
	g_hash_table_foreach(allhash, (GHFunc)unmatched_check_uid, &u);

	/* copy any changes so we can raise them outside the lock */
	if (camel_folder_change_info_changed(folder_unmatched->changes)) {
		unmatched_changes = folder_unmatched->changes;
		folder_unmatched->changes = camel_folder_change_info_new();
	}

	if (camel_folder_change_info_changed(vf->changes)) {
		vf_changes = vf->changes;
		vf->changes = camel_folder_change_info_new();
	}

	CAMEL_VEE_FOLDER_UNLOCK(folder_unmatched, summary_lock);
	CAMEL_VEE_FOLDER_UNLOCK(vf, summary_lock);

	g_hash_table_destroy(matchhash);
	g_hash_table_destroy(allhash);
	/* if expression not set, we only had a null list */
	if (vf->expression == NULL)
		g_ptr_array_free(match, TRUE);
	else
		camel_folder_search_free(f, match);
	camel_folder_free_uids(f, all);

	if (unmatched_changes) {
		camel_object_trigger_event((CamelObject *)folder_unmatched, "folder_changed", unmatched_changes);
		camel_folder_change_info_free(unmatched_changes);
	}

	if (vf_changes) {
		camel_object_trigger_event((CamelObject *)vf, "folder_changed", vf_changes);
		camel_folder_change_info_free(vf_changes);
	}

	return 0;
}

/*

  (match-folder "folder1" "folder2")

 */


/* must be called with summary_lock held */
static void
vee_folder_change_match(CamelVeeFolder *vf, CamelVeeMessageInfo *vinfo, const CamelMessageInfo *info)
{
	CamelFlag *flag;
	CamelTag *tag;

	d(printf("changing match %s\n", camel_message_info_uid(vinfo)));

	vinfo->info.flags = info->flags;
	camel_flag_list_free(&vinfo->info.user_flags);
	flag = info->user_flags;
	while (flag) {
		camel_flag_set(&vinfo->info.user_flags, flag->name, TRUE);
		flag = flag->next;
	}
	camel_tag_list_free(&vinfo->info.user_tags);
	tag = info->user_tags;
	while (tag) {
		camel_tag_set(&vinfo->info.user_tags, tag->name, tag->value);
		tag = tag->next;
	}
	camel_folder_change_info_change_uid(vf->changes, camel_message_info_uid(vinfo));
}

static void
folder_changed(CamelFolder *sub, CamelFolderChangeInfo *changes, CamelVeeFolder *vf)
{
	CamelFolder *folder = (CamelFolder *)vf;
	char *vuid, hash[8];
	CamelVeeMessageInfo *vinfo;
	int i;
	CamelMessageInfo *info;
	char *oldkey;
	int n;
	CamelFolderChangeInfo *vf_changes = NULL, *unmatched_changes = NULL;

	camel_vee_folder_hash_folder(sub, hash);

	/* if not auto-updating, only propagate changed/removed events, not added items */
	if ((vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO) == 0) {

		CAMEL_VEE_FOLDER_LOCK(vf, changed_lock);
		/* add this folder to our changed folders list if we have stuff we can't catch easily */
		/* Unfortuantely if its a change that doesn't affect the match, we're still going to
		   rerun it :( */
		if (changes->uid_changed->len > 0 || changes->uid_added->len > 0)
			if (g_list_find(vf->priv->folders_changed, sub) != NULL)
				vf->priv->folders_changed = g_list_prepend(vf->priv->folders_changed, sub);

		CAMEL_VEE_FOLDER_UNLOCK(vf, changed_lock);

		CAMEL_VEE_FOLDER_LOCK(vf, summary_lock);
		CAMEL_VEE_FOLDER_LOCK(folder_unmatched, summary_lock);

		for (i=0;i<changes->uid_changed->len;i++) {
			info = camel_folder_get_message_info(sub, changes->uid_changed->pdata[i]);
			vuid = g_strdup_printf("%.8s%s", hash, (char *)changes->uid_changed->pdata[i]);
			vinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, vuid);
			if (vinfo && info)
				vee_folder_change_match(vf, vinfo, info);
			g_free(vuid);
			if (info)
				camel_folder_free_message_info(sub, info);
			if (vinfo)
				camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)vinfo);
		}

		for (i=0;i<changes->uid_removed->len;i++) {
			vuid = g_strdup_printf("%.8s%s", hash, (char *)changes->uid_removed->pdata[i]);
			vinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, vuid);
			if (vinfo) {
				camel_folder_change_info_remove_uid(vf->changes, vuid);
				camel_folder_summary_remove(folder->summary, (CamelMessageInfo *)vinfo);
				camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)vinfo);

				if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0) {
					if (g_hash_table_lookup_extended(unmatched_uids, vuid, (void **)&oldkey, (void **)&n)) {
						g_hash_table_remove(unmatched_uids, oldkey);
						g_free(oldkey);
					}
					camel_folder_summary_remove_uid(((CamelFolder *)folder_unmatched)->summary, vuid);
				}

			}
			g_free(vuid);
		}

		if (camel_folder_change_info_changed(folder_unmatched->changes)) {
			unmatched_changes = folder_unmatched->changes;
			folder_unmatched->changes = camel_folder_change_info_new();
		}
		
		if (camel_folder_change_info_changed(vf->changes)) {
			vf_changes = vf->changes;
			vf->changes = camel_folder_change_info_new();
		}

		CAMEL_VEE_FOLDER_UNLOCK(folder_unmatched, summary_lock);
		CAMEL_VEE_FOLDER_UNLOCK(vf, summary_lock);

		if (unmatched_changes) {
			camel_object_trigger_event((CamelObject *)folder_unmatched, "folder_changed", unmatched_changes);
			camel_folder_change_info_free(unmatched_changes);
		}
		
		if (vf_changes) {
			camel_object_trigger_event((CamelObject *)vf, "folder_changed", vf_changes);
			camel_folder_change_info_free(vf_changes);
		}

		return;
	}

	/* if we are autoupdating, then do the magic */
	/* FIXME: This should be optimised to be incremental, but its just too much work right now to validate it */
	vee_folder_build_folder(vf, sub, NULL);

#if 0
	/* assume its faster to search a long list in whole, than by part */
	if (changes && (changes->uid_added->len + changes->uid_changed->len) < 500) {
		gboolean match;

		/* FIXME: We dont search body contents with this search, so, it isn't as
		   useful as it might be.
		   We shold probably just perform a whole search if we need to, i.e. there
		   are added items.  Changed items we are unlikely to want to remove immediately
		   anyway, although I guess it might be useful.
		   Removed items can always just be removed.
		*/

		/* see if added ones now match us */
		for (i=0;i<changes->uid_added->len;i++) {
			info = camel_folder_get_message_info(sub, changes->uid_added->pdata[i]);
			if (info) {
				camel_folder_search_set_folder(vf->search, sub);
				match = camel_folder_search_match_expression(vf->search, vf->expression, info, NULL);
				if (match)
					vinfo = vee_folder_add_change(vf, sub, info);
				camel_folder_free_message_info(sub, info);
			}
		}

		/* check if changed ones still match */
		for (i=0;i<changes->uid_changed->len;i++) {
			info = camel_folder_get_message_info(sub, changes->uid_changed->pdata[i]);
			vuid = g_strdup_printf("%p:%s", sub, (char *)changes->uid_changed->pdata[i]);
			vinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, vuid);
			if (info) {
				camel_folder_search_set_folder(vf->search, sub);
				match = camel_folder_search_match_expression(vf->search, vf->expression, info, NULL);
				if (vinfo) {
					if (!match)
						vfolder_remove_match(vf, vinfo);
					else
						vfolder_change_match(vf, vinfo, info);
				} else if (match)
					vee_folder_add_change(vf, sub, info);
				camel_folder_free_message_info(sub, info);
			} else if (vinfo)
				vfolder_remove_match(vf, vinfo);

			if (vinfo)
				camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)vinfo);

			g_free(vuid);
		}

		/* mirror removes directly, if they used to match */
		for (i=0;i<changes->uid_removed->len;i++) {
			vuid = g_strdup_printf("%p:%s", sub, (char *)changes->uid_removed->pdata[i]);
			vinfo = (CamelVeeMessageInfo *)camel_folder_summary_uid(folder->summary, vuid);
			if (vinfo) {
				vfolder_remove_match(vf, vinfo);
				camel_folder_summary_info_free(folder->summary, (CamelMessageInfo *)vinfo);
			}
			g_free(vuid);
		}
	} else {
		vee_folder_build_folder(vf, sub, NULL);
	}
#endif
}

/* track flag changes in the summary, we just promote it to a folder_changed event */
static void
message_changed(CamelFolder *f, const char *uid, CamelVeeFolder *vf)
{
	CamelFolderChangeInfo *changes;

	changes = camel_folder_change_info_new();
	camel_folder_change_info_change_uid(changes, uid);
	folder_changed(f, changes, vf);
	camel_folder_change_info_free(changes);
}
