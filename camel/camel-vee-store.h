/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#ifndef _CAMEL_VEE_STORE_H
#define _CAMEL_VEE_STORE_H

#include <glib.h>
#include <camel/camel-store.h>

#define CAMEL_VEE_STORE(obj)         CAMEL_CHECK_CAST (obj, camel_vee_store_get_type (), CamelVeeStore)
#define CAMEL_VEE_STORE_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_vee_store_get_type (), CamelVeeStoreClass)
#define CAMEL_IS_VEE_STORE(obj)      CAMEL_CHECK_TYPE (obj, camel_vee_store_get_type ())

typedef struct _CamelVeeStore      CamelVeeStore;
typedef struct _CamelVeeStoreClass CamelVeeStoreClass;

/* open mode for folder, vee folder auto-update */
#define CAMEL_STORE_VEE_FOLDER_AUTO (1<<16)

struct _CamelVeeStore {
	CamelStore parent;

	struct _CamelVeeStorePrivate *priv;
};

struct _CamelVeeStoreClass {
	CamelStoreClass parent_class;
};

guint		camel_vee_store_get_type	(void);
CamelVeeStore      *camel_vee_store_new	(void);

#endif /* ! _CAMEL_VEE_STORE_H */
