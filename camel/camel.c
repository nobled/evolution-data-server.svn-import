/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* 
 *
 * Author : 
 *  Bertrand Guiheneuf <Bertrand.Guiheneuf@aful.org>
 *
 * Copyright 1999 International GNOME Support (http://www.gnome-support.com) .
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
#include "camel.h"

gint
camel_init()
{
#ifdef G_THREADS_ENABLED	
	g_thread_init (NULL);
#else  /* G_THREADS_ENABLED */
	printf ("Threads are not supported by glib\n");
#endif /* G_THREADS_ENABLED */

	//return data_wrapper_repository_init ();
	
}
