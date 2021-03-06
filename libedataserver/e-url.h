/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * e-url.h
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Developed by Jon Trowbridge <trow@ximian.com>
 *              Rodrigo Moya   <rodrigo@ximian.com>
 */

/*
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
 * USA.
 */

#ifndef __E_URL_H__
#define __E_URL_H__

#include <glib.h>

G_BEGIN_DECLS

char *e_url_shroud (const char *url);
gboolean e_url_equal (const char *url1, const char *url2);

/**
 * EUri:
 * @protocol: The protocol to use.
 * @user: A user name.
 * @authmech: The authentication mechanism.
 * @passwd: The connection password.
 * @host: The host name.
 * @port: The port number.
 * @path: The file path on the host.
 * @params: Additional parameters.
 * @query:
 * @fragment:
 *
 * A structure representing a URI.
 **/
typedef struct {
	char  *protocol;
	char  *user;
	char  *authmech;
	char  *passwd;
	char  *host;
	int    port;
	char  *path;
	GData *params;
	char  *query;
	char  *fragment;
} EUri;

EUri       *e_uri_new       (const char *uri_string);
void        e_uri_free      (EUri *uri);
const char *e_uri_get_param (EUri *uri, const char *name);
EUri       *e_uri_copy      (EUri *uri);
char       *e_uri_to_string (EUri *uri, gboolean show_password);

G_END_DECLS

#endif /* __E_URL_H__ */

