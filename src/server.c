/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* server.c
 *
 * Copyright (C) 2000-2003, Ximian, Inc.
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
 * Author: Nat Friedman (nat@ximian.com)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* define this if you need/want to be able to send USR2 to server and
   get a list of the active backends */
/*#define DEBUG_BACKENDS*/

#include <stdlib.h>
#ifdef DEBUG_BACKENDS
#include <sys/signal.h>
#endif

#include <glib.h>
#include <libgnome/gnome-init.h>
#include <bonobo-activation/bonobo-activation.h>
#include <libgnomevfs/gnome-vfs-init.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-i18n.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-generic-factory.h>

#include <libedata-book/e-data-book-factory.h>
#include <backends/file/e-book-backend-file.h>
#include <backends/vcf/e-book-backend-vcf.h>
#ifdef HAVE_LDAP
#include <backends/ldap/e-book-backend-ldap.h>
#endif
#include <backends/groupwise/e-book-backend-groupwise.h>

#include <libedata-cal/e-data-cal-factory.h>
#include <backends/file/e-cal-backend-file-events.h>
#include <backends/file/e-cal-backend-file-todos.h>
#include <backends/groupwise/e-cal-backend-groupwise.h>
#include <backends/http/e-cal-backend-http.h>
#include <backends/contacts/e-cal-backend-contacts.h>

#include "server-interface-check.h"
#include "server-logging.h"

#define E_DATA_SERVER_INTERFACE_CHECK_OAF_ID "OAFIID:GNOME_Evolution_DataServer_InterfaceCheck"
#define E_DATA_SERVER_LOGGING_OAF_ID "OAFIID:GNOME_Evolution_DataServer_Logging"

#define E_DATA_CAL_FACTORY_OAF_ID "OAFIID:GNOME_Evolution_DataServer_CalFactory:" BASE_VERSION
#define E_DATA_BOOK_FACTORY_OAF_ID "OAFIID:GNOME_Evolution_DataServer_BookFactory:" BASE_VERSION

/* The and addressbook calendar factories */

static EDataCalFactory *e_data_cal_factory;

static EDataBookFactory *e_data_book_factory;

/* The other interfaces we implement */

static ServerLogging *logging_iface;
static ServerInterfaceCheck *interface_check_iface;

/* Timeout interval in milliseconds for termination */
#define EXIT_TIMEOUT 5000

/* Timeout ID for termination handler */
static guint termination_handler_id;

static GStaticMutex termination_lock = G_STATIC_MUTEX_INIT;



/* Termination */

/* Termination handler.  Checks if both factories have zero running backends,
 * and if so terminates the program.
 */
static gboolean
termination_handler (gpointer data)
{
	if (e_data_cal_factory_get_n_backends (e_data_cal_factory) == 0 &&
	    e_data_book_factory_get_n_backends (e_data_book_factory) == 0) {
		g_message ("termination_handler(): Terminating the Server.  Have a nice day.");
		bonobo_main_quit ();
	}

	termination_handler_id = 0;
	return FALSE;
}

/* Queues a timeout for handling termination of Server */
static void
queue_termination (void)
{
	g_static_mutex_lock (&termination_lock);
	if (termination_handler_id)
		g_source_remove (termination_handler_id);

	termination_handler_id = g_timeout_add (EXIT_TIMEOUT, termination_handler, NULL);
	g_static_mutex_unlock (&termination_lock);
}



static void
last_book_gone_cb (EDataBookFactory *factory, gpointer data)
{
	queue_termination ();
}

static gboolean
setup_books (void)
{
	e_data_book_factory = e_data_book_factory_new ();

	if (!e_data_book_factory)
		return FALSE;

	e_data_book_factory_register_backend (
		e_data_book_factory, "file", e_book_backend_file_new);

	e_data_book_factory_register_backend (
		e_data_book_factory, "vcf", e_book_backend_vcf_new);

#ifdef HAVE_LDAP
	e_data_book_factory_register_backend (
		e_data_book_factory, "ldap", e_book_backend_ldap_new);
#endif

	e_data_book_factory_register_backend (
		e_data_book_factory, "groupwise", e_book_backend_groupwise_new);

	g_signal_connect (e_data_book_factory,
			  "last_book_gone",
			  G_CALLBACK (last_book_gone_cb),
			  NULL);

	if (!e_data_book_factory_activate (e_data_book_factory, E_DATA_BOOK_FACTORY_OAF_ID)) {
		bonobo_object_unref (BONOBO_OBJECT (e_data_book_factory));
		e_data_book_factory = NULL;
		return FALSE;
	}

	return TRUE;
}


/* Personal calendar server */

/* Callback used when the calendar factory has no more running backends */
static void
last_calendar_gone_cb (EDataCalFactory *factory, gpointer data)
{
	queue_termination ();
}

/* Creates the calendar factory object and registers it */
static gboolean
setup_cals (void)
{
	e_data_cal_factory = e_data_cal_factory_new ();

	if (!e_data_cal_factory) {
		g_warning (G_STRLOC ": Could not create the calendar factory");
		return FALSE;
	}

	e_data_cal_factory_register_method (e_data_cal_factory, "file", ICAL_VEVENT_COMPONENT, E_TYPE_CAL_BACKEND_FILE_EVENTS);
	e_data_cal_factory_register_method (e_data_cal_factory, "file", ICAL_VTODO_COMPONENT, E_TYPE_CAL_BACKEND_FILE_TODOS);
	e_data_cal_factory_register_method (e_data_cal_factory, "groupwise", ICAL_VEVENT_COMPONENT, E_TYPE_CAL_BACKEND_GROUPWISE);
	e_data_cal_factory_register_method (e_data_cal_factory, "groupwise", ICAL_VTODO_COMPONENT, E_TYPE_CAL_BACKEND_GROUPWISE);
	e_data_cal_factory_register_method (e_data_cal_factory, "webcal", ICAL_VEVENT_COMPONENT, E_TYPE_CAL_BACKEND_HTTP);
	e_data_cal_factory_register_method (e_data_cal_factory, "contacts", ICAL_VEVENT_COMPONENT, E_TYPE_CAL_BACKEND_CONTACTS);

	if (!e_data_cal_factory_register_storage (e_data_cal_factory, E_DATA_CAL_FACTORY_OAF_ID)) {
		bonobo_object_unref (BONOBO_OBJECT (e_data_cal_factory));
		e_data_cal_factory = NULL;
		return FALSE;
	}

	g_signal_connect (G_OBJECT (e_data_cal_factory),
			  "last_calendar_gone",
			  G_CALLBACK (last_calendar_gone_cb),
			  NULL);

	return TRUE;
}


/* Logging iface.  */
static gboolean
setup_logging (void)
{
	int result;

	logging_iface = server_logging_new ();

	server_logging_register_domain (logging_iface, NULL);
	server_logging_register_domain (logging_iface, "Gdk");
	server_logging_register_domain (logging_iface, "Gtk");
	server_logging_register_domain (logging_iface, "GdkPixbuf");
	server_logging_register_domain (logging_iface, "GLib");
	server_logging_register_domain (logging_iface, "GModule");
	server_logging_register_domain (logging_iface, "GLib-GObject");
	server_logging_register_domain (logging_iface, "GThread");

	server_logging_register_domain (logging_iface, "evolution-data-server");
	server_logging_register_domain (logging_iface, "libebookbackend");
	server_logging_register_domain (logging_iface, "libecalbackendfile");

	result = bonobo_activation_active_server_register (E_DATA_SERVER_LOGGING_OAF_ID,
							   BONOBO_OBJREF (logging_iface));

	return result == Bonobo_ACTIVATION_REG_SUCCESS;
}


/* Interface check iface.  */

static gboolean
setup_interface_check (void)
{
	int result;

	interface_check_iface = server_interface_check_new ();
	result = bonobo_activation_active_server_register (E_DATA_SERVER_INTERFACE_CHECK_OAF_ID,
							   BONOBO_OBJREF (interface_check_iface));

	return result == Bonobo_ACTIVATION_REG_SUCCESS;
}



#ifdef DEBUG_BACKENDS
static void
dump_backends (int signal)
{
	e_data_book_factory_dump_active_backends (e_data_book_factory);
	e_data_cal_factory_dump_active_backends (e_data_cal_factory);
}
#endif

int
main (int argc, char **argv)
{
	gboolean did_books=FALSE, did_cals=FALSE;

	bindtextdomain (GETTEXT_PACKAGE, EVOLUTION_LOCALEDIR);
	textdomain (GETTEXT_PACKAGE);

	g_message ("Starting server");

#ifdef DEBUG_BACKENDS
	signal (SIGUSR2, dump_backends);
#endif

       	gnome_program_init (PACKAGE, VERSION,
			    LIBGNOME_MODULE,
			    argc, argv,
			    GNOME_PROGRAM_STANDARD_PROPERTIES, NULL);

	bonobo_init_full (&argc, argv,
			  bonobo_activation_orb_get(),
			  CORBA_OBJECT_NIL,
			  CORBA_OBJECT_NIL);

	if (!( (did_books = setup_books ())
	       && (did_cals = setup_cals ())
		    )) {

		const gchar *failed = NULL;

		if (!did_books)
			failed = "BOOKS";
		else if (!did_cals)
			failed = "CALS";

		g_error (G_STRLOC ": could not initialize Server service \"%s\"; terminating", failed);

		if (e_data_book_factory) {
			bonobo_object_unref (BONOBO_OBJECT (e_data_book_factory));
			e_data_book_factory = NULL;
		}

		if (e_data_cal_factory) {
			bonobo_object_unref (BONOBO_OBJECT (e_data_cal_factory));
			e_data_cal_factory = NULL;
		}
		exit (EXIT_FAILURE);
	}

	if ( setup_logging ()) {
			if ( setup_interface_check ()) {
				g_message ("Server up and running");

				bonobo_main ();
			} else
				g_error (G_STRLOC "Cannot register DataServer::InterfaceCheck object");
	} else
		g_error (G_STRLOC "Cannot register DataServer::Logging object");

	bonobo_object_unref (BONOBO_OBJECT (e_data_cal_factory));
	e_data_cal_factory = NULL;

	bonobo_object_unref (BONOBO_OBJECT (e_data_book_factory));
	e_data_book_factory = NULL;

	bonobo_object_unref (BONOBO_OBJECT (logging_iface));
	logging_iface = NULL;

	bonobo_object_unref (BONOBO_OBJECT (interface_check_iface));
	interface_check_iface = NULL;

	gnome_vfs_shutdown ();

	return 0;
}
