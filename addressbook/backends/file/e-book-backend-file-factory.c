/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-file-factory.c - File contact backend factory.
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Chris Toshok <toshok@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pthread.h>
#include <string.h>

#include <libedataserver/e-data-server-module.h>
#include "libedata-book/e-book-backend-factory.h"
#include "e-book-backend-file.h"

E_BOOK_BACKEND_FACTORY_SIMPLE (file, File, e_book_backend_file_new)

static GType  file_type;

void
eds_module_initialize (GTypeModule *module)
{
	file_type = _file_factory_get_type (module);
}

void
eds_module_shutdown   (void)
{
}

void
eds_module_list_types (const GType **types, int *num_types)
{
	*types = & file_type;
	*num_types = 1;
}

