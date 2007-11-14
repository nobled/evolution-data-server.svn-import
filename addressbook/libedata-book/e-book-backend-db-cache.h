/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Copyright (C) 2004 Novell, Inc.
 *
 * Authors: Devashish Sharma <sdevashish@novell.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef E_BOOK_BACKEND_DB_CACHE_H
#define E_BOOK_BACKEND_DB_CACHE_H

#include <libebook/e-contact.h>
#include "db.h"

EContact* e_book_backend_db_cache_get_contact (DB *db, const char *uid);
void string_to_dbt(const char *str, DBT *dbt);
char *e_book_backend_db_cache_get_filename(DB *db);
void e_book_backend_db_cache_set_filename(DB *db, const char *filename);
gboolean e_book_backend_db_cache_add_contact (DB *db,
					   EContact *contact);
gboolean e_book_backend_db_cache_remove_contact (DB *db,
					      const char *uid);
gboolean e_book_backend_db_cache_check_contact (DB *db, const char *uid);
GList*   e_book_backend_db_cache_get_contacts (DB *db, const char *query);
gboolean e_book_backend_db_cache_exists (const char *uri);
void     e_book_backend_db_cache_set_populated (DB *db);
gboolean e_book_backend_db_cache_is_populated (DB *db);
GPtrArray* e_book_backend_db_cache_search (DB *db, const char *query);




G_END_DECLS

#endif

