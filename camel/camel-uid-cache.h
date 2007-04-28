/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-uid-cache.h: UID caching code. */

/* 
 * Authors:
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 2000 Ximian, Inc. (www.ximian.com)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifndef CAMEL_UID_CACHE_H
#define CAMEL_UID_CACHE_H 1

#include <glib.h>

#include <stdio.h>
#include <sys/types.h>

G_BEGIN_DECLS

typedef struct {
	char *filename;
	GHashTable *uids;
	unsigned int level;
	size_t expired;
	size_t size;
	int fd;
} CamelUIDCache;

CamelUIDCache *camel_uid_cache_new (const char *filename);
gboolean camel_uid_cache_save (CamelUIDCache *cache);
void camel_uid_cache_destroy (CamelUIDCache *cache);

GPtrArray *camel_uid_cache_get_new_uids (CamelUIDCache *cache, GPtrArray *uids);

void camel_uid_cache_save_uid (CamelUIDCache *cache, const char *uid);
void camel_uid_cache_free_uids (GPtrArray *uids);

G_END_DECLS

#endif /* CAMEL_UID_CACHE_H */
