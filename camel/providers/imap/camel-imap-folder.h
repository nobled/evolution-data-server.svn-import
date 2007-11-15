/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-folder.h: class for an imap folder */

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


#ifndef CAMEL_IMAP_FOLDER_H
#define CAMEL_IMAP_FOLDER_H 1

#include "camel-imap-types.h"
#include <camel/camel-disco-folder.h>
#include <camel/camel-folder-search.h>

#define CAMEL_IMAP_FOLDER_TYPE     (camel_imap_folder_get_type ())
#define CAMEL_IMAP_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_IMAP_FOLDER_TYPE, CamelImapFolder))
#define CAMEL_IMAP_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_IMAP_FOLDER_TYPE, CamelImapFolderClass))
#define CAMEL_IS_IMAP_FOLDER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_IMAP_FOLDER_TYPE))

G_BEGIN_DECLS

struct _CamelImapFolder {
	CamelDiscoFolder parent_object;

	struct _CamelImapFolderPrivate *priv;

	CamelFolderSearch *search;
	CamelImapMessageCache *cache;

	unsigned int need_rescan:1;
	unsigned int need_refresh:1;
	unsigned int read_only:1;
};

typedef struct {
	CamelDiscoFolderClass parent_class;

	/* Virtual methods */

} CamelImapFolderClass;


/* public methods */
CamelFolder *camel_imap_folder_new (CamelStore *parent,
				    const char *folder_name,
				    const char *folder_dir,
				    CamelException *ex);

void camel_imap_folder_selected (CamelFolder *folder,
				 CamelImapResponse *response,
				 CamelException *ex);

void camel_imap_folder_changed (CamelFolder *folder, int exists,
				GArray *expunged, CamelException *ex);

CamelStream *camel_imap_folder_fetch_data (CamelImapFolder *imap_folder,
					   const char *uid,
					   const char *section_text,
					   gboolean cache_only,
					   CamelException *ex);

/* Standard Camel function */
CamelType camel_imap_folder_get_type (void);

G_END_DECLS

#endif /* CAMEL_IMAP_FOLDER_H */
