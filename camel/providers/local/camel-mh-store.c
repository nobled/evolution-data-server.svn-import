/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "camel-mh-store.h"
#include "camel-mh-folder.h"
#include "camel-exception.h"
#include "camel-url.h"

static CamelLocalStoreClass *parent_class = NULL;

/* Returns the class for a CamelMhStore */
#define CMHS_CLASS(so) CAMEL_MH_STORE_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CMHF_CLASS(so) CAMEL_MH_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static CamelFolder *get_folder(CamelStore * store, const char *folder_name, guint32 flags, CamelException * ex);
static void delete_folder(CamelStore * store, const char *folder_name, CamelException * ex);

static void camel_mh_store_class_init(CamelObjectClass * camel_mh_store_class)
{
	CamelStoreClass *camel_store_class = CAMEL_STORE_CLASS(camel_mh_store_class);
	/*CamelServiceClass *camel_service_class = CAMEL_SERVICE_CLASS(camel_mh_store_class);*/

	parent_class = (CamelLocalStoreClass *)camel_type_get_global_classfuncs(camel_local_store_get_type());

	/* virtual method overload, use defaults for most */
	camel_store_class->get_folder = get_folder;
	camel_store_class->delete_folder = delete_folder;
}

CamelType camel_mh_store_get_type(void)
{
	static CamelType camel_mh_store_type = CAMEL_INVALID_TYPE;

	if (camel_mh_store_type == CAMEL_INVALID_TYPE) {
		camel_mh_store_type = camel_type_register(CAMEL_LOCAL_STORE_TYPE, "CamelMhStore",
							  sizeof(CamelMhStore),
							  sizeof(CamelMhStoreClass),
							  (CamelObjectClassInitFunc) camel_mh_store_class_init,
							  NULL,
							  NULL,
							  NULL);
	}

	return camel_mh_store_type;
}

static CamelFolder *get_folder(CamelStore * store, const char *folder_name, guint32 flags, CamelException * ex)
{
	char *name;
	struct stat st;

	(void) ((CamelStoreClass *)parent_class)->get_folder(store, folder_name, flags, ex);
	if (camel_exception_is_set(ex))
		return NULL;

	name = g_strdup_printf("%s%s", CAMEL_LOCAL_STORE(store)->toplevel_dir, folder_name);

	if (stat(name, &st) == -1) {
		if (errno != ENOENT) {
			camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
					     _("Could not open folder `%s':\n%s"),
					     folder_name, g_strerror(errno));
			g_free (name);
			return NULL;
		}
		if ((flags & CAMEL_STORE_FOLDER_CREATE) == 0) {
			camel_exception_setv(ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
					     _("Folder `%s' does not exist."), folder_name);
			g_free (name);
			return NULL;
		}
		if (mkdir(name, 0700) != 0) {
			camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
					     _("Could not create folder `%s':\n%s"),
					     folder_name, g_strerror(errno));
			g_free (name);
			return NULL;
		}
	} else if (!S_ISDIR(st.st_mode)) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
				     _("`%s' is not a directory."), name);
		g_free (name);
		return NULL;
	}
	g_free(name);

	return camel_mh_folder_new(store, folder_name, flags, ex);
}

static void delete_folder(CamelStore * store, const char *folder_name, CamelException * ex)
{
	char *name;

	/* remove folder directory - will fail if not empty */
	name = g_strdup_printf("%s%s", CAMEL_LOCAL_STORE(store)->toplevel_dir, folder_name);
	if (rmdir(name) == -1) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Could not delete folder `%s': %s"),
				     folder_name, strerror(errno));
		g_free(name);
		return;
	}
	g_free(name);

	/* and remove metadata */
	((CamelStoreClass *)parent_class)->delete_folder(store, folder_name, ex);
}
