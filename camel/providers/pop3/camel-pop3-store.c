/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-pop3-store.c : class for a pop3 store */

/* 
 * Authors:
 *   Dan Winship <danw@ximian.com>
 *
 * Copyright (C) 2000 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * License as published by the Free Software Foundation.
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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "camel-operation.h"

#ifdef HAVE_KRB4
/* Specified nowhere */
#define KPOP_PORT 1109

#include <krb.h>
/* MIT krb4 des.h #defines _. Sigh. We don't need it. */
#undef _

#ifdef NEED_KRB_SENDAUTH_PROTO
extern int krb_sendauth(long options, int fd, KTEXT ticket, char *service,
			char *inst, char *realm, unsigned KRB4_32 checksum,
			MSG_DAT *msg_data, CREDENTIALS *cred,
			Key_schedule schedule, struct sockaddr_in *laddr,
			struct sockaddr_in *faddr, char *version);
#endif
#endif

#include "camel-pop3-store.h"
#include "camel-pop3-folder.h"
#include "camel-stream-buffer.h"
#include "camel-tcp-stream.h"
#include "camel-session.h"
#include "camel-exception.h"
#include "camel-url.h"
#include "e-util/md5-utils.h"

/* Specified in RFC 1939 */
#define POP3_PORT 110

static CamelRemoteStoreClass *parent_class = NULL;

static void finalize (CamelObject *object);

static gboolean pop3_connect (CamelService *service, CamelException *ex);
static gboolean pop3_disconnect (CamelService *service, gboolean clean, CamelException *ex);
static GList *query_auth_types (CamelService *service, CamelException *ex);

static CamelFolder *get_folder (CamelStore *store, const char *folder_name, 
				guint32 flags, CamelException *ex);

static void init_trash (CamelStore *store);
static CamelFolder *get_trash  (CamelStore *store, CamelException *ex);

static int pop3_get_response (CamelPop3Store *store, char **ret, CamelException *ex);


static void
camel_pop3_store_class_init (CamelPop3StoreClass *camel_pop3_store_class)
{
	CamelServiceClass *camel_service_class =
		CAMEL_SERVICE_CLASS (camel_pop3_store_class);
	CamelStoreClass *camel_store_class =
		CAMEL_STORE_CLASS (camel_pop3_store_class);

	parent_class = CAMEL_REMOTE_STORE_CLASS(camel_type_get_global_classfuncs 
						(camel_remote_store_get_type ()));

	/* virtual method overload */
	camel_service_class->query_auth_types = query_auth_types;
	camel_service_class->connect = pop3_connect;
	camel_service_class->disconnect = pop3_disconnect;

	camel_store_class->get_folder = get_folder;
	camel_store_class->init_trash = init_trash;
	camel_store_class->get_trash = get_trash;
}



static void
camel_pop3_store_init (gpointer object, gpointer klass)
{
	CamelRemoteStore *remote_store = CAMEL_REMOTE_STORE (object);

	remote_store->default_port = 110;
	/* FIXME: what should this port be?? */
	remote_store->default_ssl_port = 995;
}

CamelType
camel_pop3_store_get_type (void)
{
	static CamelType camel_pop3_store_type = CAMEL_INVALID_TYPE;

	if (!camel_pop3_store_type) {
		camel_pop3_store_type = camel_type_register (CAMEL_REMOTE_STORE_TYPE, "CamelPop3Store",
							     sizeof (CamelPop3Store),
							     sizeof (CamelPop3StoreClass),
							     (CamelObjectClassInitFunc) camel_pop3_store_class_init,
							     NULL,
							     (CamelObjectInitFunc) camel_pop3_store_init,
							     finalize);
	}

	return camel_pop3_store_type;
}

static void
finalize (CamelObject *object)
{
	CamelPop3Store *pop3_store = CAMEL_POP3_STORE (object);

	if (pop3_store->apop_timestamp)
		g_free (pop3_store->apop_timestamp);
	if (pop3_store->implementation)
		g_free (pop3_store->implementation);
}

static gboolean
connect_to_server (CamelService *service, CamelException *ex)
{
	CamelPop3Store *store = CAMEL_POP3_STORE (service);
	char *buf, *apoptime, *apopend;
	int status;
	gboolean result;

#ifdef HAVE_KRB4
	gboolean set_port = FALSE, kpop;

	kpop = (service->url->authmech &&
		!strcmp (service->url->authmech, "+KPOP"));

	if (kpop && service->url->port == 0) {
		set_port = TRUE;
		service->url->port = KPOP_PORT;
	}
#endif

  	result = CAMEL_SERVICE_CLASS (parent_class)->connect (service, ex);

#ifdef HAVE_KRB4
	if (set_port)
		service->url->port = 0;
#endif

	if (result == FALSE)
		return FALSE;

#ifdef HAVE_KRB4
	if (kpop) {
		KTEXT_ST ticket_st;
		MSG_DAT msg_data;
		CREDENTIALS cred;
		Key_schedule schedule;
		struct hostent *h;
		int fd;

		h = camel_service_gethost (service, ex);

		fd = GPOINTER_TO_INT (camel_tcp_stream_get_socket (CAMEL_TCP_STREAM (CAMEL_REMOTE_STORE (service)->ostream)));
		status = krb_sendauth (0, fd, &ticket_st, "pop", h->h_name,
				       krb_realmofhost (h->h_name), 0,
				       &msg_data, &cred, schedule,
				       NULL, NULL, "KPOPV0.1");
		camel_free_host (h);
		if (status != KSUCCESS) {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					      _("Could not authenticate to "
						"KPOP server: %s"),
					      krb_err_txt[status]);
			return FALSE;
		}

		if (!service->url->passwd)
			service->url->passwd = g_strdup (service->url->user);
	}
#endif /* HAVE_KRB4 */

	/* Read the greeting, check status */
	status = pop3_get_response (store, &buf, ex);
	switch (status) {
	case CAMEL_POP3_ERR:
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      _("Could not connect to server: %s"),
				      buf);
		g_free (buf);
		/* fall through */
	case CAMEL_POP3_FAIL:
		return FALSE;
	}

	if (buf) {
		apoptime = strchr (buf, '<');
		apopend = apoptime ? strchr (apoptime, '>') : NULL;
		if (apopend) {
			store->apop_timestamp =
				g_strndup (apoptime, apopend - apoptime + 1);
			memmove (apoptime, apopend + 1, strlen (apopend + 1));
		}
		store->implementation = buf;
	}

	/* Check extensions */
	store->login_delay = -1;
	store->supports_top = -1;
	store->supports_uidl = -1;
	store->expires = -1;

	status = camel_pop3_command (store, NULL, ex, "CAPA");
	if (status == CAMEL_POP3_OK) {
		char *p;
		int len;

		buf = camel_pop3_command_get_additional_data (store, 0, ex);
		if (camel_exception_is_set (ex))
			return FALSE;

		p = buf;
		while (*p) {
			len = strcspn (p, "\n");
			if (!strncmp (p, "IMPLEMENTATION ", 15)) {
				g_free (store->implementation);
				store->implementation =
					g_strndup (p + 15, len - 15);
			} else if (len == 3 && !strncmp (p, "TOP", 3))
				store->supports_top = TRUE;
			else if (len == 4 && !strncmp (p, "UIDL", 4))
				store->supports_uidl = TRUE;
			else if (!strncmp (p, "LOGIN-DELAY ", 12))
				store->login_delay = atoi (p + 12);
			else if (!strncmp (p, "EXPIRE NEVER", 12))
				store->expires = FALSE;
			else if (!strncmp (p, "EXPIRE ", 7))
				store->expires = TRUE;

			p += len;
			if (*p)
				p++;
		}

		g_free (buf);
	}

	return TRUE;
}

extern CamelServiceAuthType camel_pop3_password_authtype;
extern CamelServiceAuthType camel_pop3_apop_authtype;
#ifdef HAVE_KRB4
extern CamelServiceAuthType camel_pop3_kpop_authtype;
#endif

static GList *
query_auth_types (CamelService *service, CamelException *ex)
{
	CamelPop3Store *store = CAMEL_POP3_STORE (service);
	GList *types = NULL;
	gboolean passwd = TRUE, apop = TRUE;
#ifdef HAVE_KRB4
	gboolean kpop;
#endif

        types = CAMEL_SERVICE_CLASS (parent_class)->query_auth_types (service, ex);
	if (camel_exception_is_set (ex))
		return types;

	passwd = connect_to_server (service, NULL);
	apop = store->apop_timestamp != NULL;
	if (passwd)
		pop3_disconnect (service, TRUE, NULL);

#ifdef HAVE_KRB4
	service->url->authmech = "+KPOP";
	kpop = connect_to_server (service, NULL);
	service->url->authmech = NULL;
	if (kpop)
		pop3_disconnect (service, TRUE, NULL);
#endif

	if (passwd)
		types = g_list_append (types, &camel_pop3_password_authtype);
	if (apop)
		types = g_list_append (types, &camel_pop3_apop_authtype);
#ifdef HAVE_KRB4
	if (kpop)
		types = g_list_append (types, &camel_pop3_kpop_authtype);
#endif

	if (!types) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      _("Could not connect to POP server on "
					"%s."), service->url->host);
	}
	return types;
}

/**
 * camel_pop3_store_expunge:
 * @store: the store
 * @ex: a CamelException
 *
 * Expunge messages from the store. This will result in the connection
 * being closed, which may cause later commands to fail if they can't
 * reconnect.
 **/
void
camel_pop3_store_expunge (CamelPop3Store *store, CamelException *ex)
{
	camel_pop3_command (store, NULL, ex, "QUIT");
	camel_service_disconnect (CAMEL_SERVICE (store), FALSE, ex);
}


static gboolean
pop3_try_authenticate (CamelService *service, const char *errmsg,
		       CamelException *ex)
{
	CamelPop3Store *store = (CamelPop3Store *)service;
	int status;
	char *msg;

	/* The KPOP code will have set the password to be the username
	 * in connect_to_server. Password and APOP are the only other
	 * cases, and they both need a password. So if there's no
	 * password stored, query for it.
	 */
	if (!service->url->passwd) {
		char *prompt;

		prompt = g_strdup_printf (_("%sPlease enter the POP3 password "
					    "for %s@%s"), errmsg ? errmsg : "",
					  service->url->user,
					  service->url->host);
		service->url->passwd = camel_session_get_password (
			camel_service_get_session (service),
			prompt, TRUE, service, "password", ex);
		g_free (prompt);
		if (!service->url->passwd)
			return FALSE;
	}

	if (!service->url->authmech || !strcmp (service->url->authmech, "+KPOP")) {
		status = camel_pop3_command (store, &msg, ex, "USER %s",
					     service->url->user);
		switch (status) {
		case CAMEL_POP3_ERR:
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
					      _("Unable to connect to POP "
						"server.\nError sending "
						"username: %s"),
					      msg ? msg : _("(Unknown)"));
			g_free (msg);
			/*fallll*/
		case CAMEL_POP3_FAIL:
			return FALSE;
		}
		g_free (msg);

		status = camel_pop3_command (store, &msg, ex, "PASS %s",
					     service->url->passwd);
	} else if (!strcmp (service->url->authmech, "+APOP")
		   && store->apop_timestamp) {
		char *secret, md5asc[33], *d;
		unsigned char md5sum[16], *s;

		secret = g_strdup_printf ("%s%s", store->apop_timestamp,
					  service->url->passwd);
		md5_get_digest (secret, strlen (secret), md5sum);
		g_free (secret);

		for (s = md5sum, d = md5asc; d < md5asc + 32; s++, d += 2)
			sprintf (d, "%.2x", *s);

		status = camel_pop3_command (store, &msg, ex, "APOP %s %s",
					     service->url->user, md5asc);
	} else {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
				     _("Unable to connect to POP server.\n"
				       "No support for requested "
				       "authentication mechanism."));
		return FALSE;
	}

	if (status == CAMEL_POP3_ERR) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE,
				      _("Unable to connect to POP server.\n"
					"Error sending password: %s"),
				      msg ? msg : _("(Unknown)"));
	}

	g_free (msg);
	return status == CAMEL_POP3_ERR;
}

static gboolean
pop3_connect (CamelService *service, CamelException *ex)
{
	char *errbuf = NULL;
	gboolean tryagain;

	if (!connect_to_server (service, ex))
		return FALSE;

	camel_exception_clear (ex);
	do {
		if (camel_exception_is_set (ex)) {
			errbuf = g_strdup_printf (
				"%s\n\n",
				camel_exception_get_description (ex));
			camel_exception_clear (ex);

			/* Uncache the password before prompting again. */
			camel_session_forget_password (
				camel_service_get_session (service),
				service, "password", ex);
			g_free (service->url->passwd);
			service->url->passwd = NULL;
		}

		tryagain = pop3_try_authenticate (service, errbuf, ex);
		g_free (errbuf);
		errbuf = NULL;
	} while (tryagain);

	if (camel_exception_is_set (ex)) {
		camel_service_disconnect (service, TRUE, ex);
		return FALSE;
	}

	return TRUE;
}

static gboolean
pop3_disconnect (CamelService *service, gboolean clean, CamelException *ex)
{
	CamelPop3Store *store = CAMEL_POP3_STORE (service);
	
	if (clean)
		camel_pop3_command (store, NULL, ex, "QUIT");
	
	if (!CAMEL_SERVICE_CLASS (parent_class)->disconnect (service, clean, ex))
		return FALSE;
	
	return TRUE;
}

static CamelFolder *
get_folder (CamelStore *store, const char *folder_name,
	    guint32 flags, CamelException *ex)
{
	if (g_strcasecmp (folder_name, "inbox") != 0) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID,
				      _("No such folder `%s'."), folder_name);
		return NULL;
	}
	return camel_pop3_folder_new (store, ex);
}

static void
init_trash (CamelStore *store)
{
	/* no-op */
	;
}

static CamelFolder *
get_trash (CamelStore *store, CamelException *ex)
{
	/* no-op */
	return NULL;
}


/**
 * camel_pop3_command: Send a command to a POP3 server.
 * @store: the POP3 store
 * @ret: a pointer to return the full server response in
 * @fmt: a printf-style format string, followed by arguments
 *
 * This command sends the command specified by @fmt and the following
 * arguments to the connected POP3 store specified by @store. It then
 * reads the server's response and parses out the status code. If
 * the caller passed a non-NULL pointer for @ret, camel_pop3_command
 * will set it to point to an buffer containing the rest of the
 * response from the POP3 server. (If @ret was passed but there was
 * no extended response, @ret will be set to NULL.) The caller must
 * free this buffer when it is done with it.
 *
 * Return value: one of CAMEL_POP3_OK (command executed successfully),
 * CAMEL_POP3_ERR (command encounted an error), or CAMEL_POP3_FAIL
 * (a protocol-level error occurred, and Camel is uncertain of the
 * result of the command.) @ex will be set if the return value is
 * CAMEL_POP3_FAIL, but *NOT* if it is CAMEL_POP3_ERR.
 **/
int
camel_pop3_command (CamelPop3Store *store, char **ret, CamelException *ex, char *fmt, ...)
{
	char *cmdbuf;
	va_list ap;
	
	va_start (ap, fmt);
	cmdbuf = g_strdup_vprintf (fmt, ap);
	va_end (ap);
	
	/* Send the command */
	if (camel_remote_store_send_string (CAMEL_REMOTE_STORE (store), ex, "%s\r\n", cmdbuf) < 0) {
		g_free (cmdbuf);
		if (ret)
			*ret = NULL;
		return CAMEL_POP3_FAIL;
	}
	g_free (cmdbuf);
	
	return pop3_get_response (store, ret, ex);
}

static int
pop3_get_response (CamelPop3Store *store, char **ret, CamelException *ex)
{
	char *respbuf;
	int status;
	
	if (camel_remote_store_recv_line (CAMEL_REMOTE_STORE (store), &respbuf, ex) < 0) {
		if (ret)
			*ret = NULL;
		return CAMEL_POP3_FAIL;
	}
	
	if (!strncmp (respbuf, "+OK", 3))
		status = CAMEL_POP3_OK;
	else if (!strncmp (respbuf, "-ERR", 4))
		status = CAMEL_POP3_ERR;
	else {
		status = CAMEL_POP3_FAIL;
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      _("Unexpected response from POP server: %s"),
				      respbuf);
	}
	
	if (ret) {
		if (status != CAMEL_POP3_FAIL) {
			*ret = strchr (respbuf, ' ');
			if (*ret)
				*ret = g_strdup (*ret + 1);
		} else
			*ret = NULL;
	}
	g_free (respbuf);
	
	return status;
}

/**
 * camel_pop3_command_get_additional_data: get "additional data" from
 * a POP3 command.
 * @store: the POP3 store
 * @total: Total bytes expected (for progress reporting), use 0 for 'unknown'.
 *
 * This command gets the additional data returned by "multi-line" POP
 * commands, such as LIST, RETR, TOP, and UIDL. This command _must_
 * be called after a successful (CAMEL_POP3_OK) call to
 * camel_pop3_command for a command that has a multi-line response.
 * The returned data is un-byte-stuffed, and has lines termined by
 * newlines rather than CR/LF pairs.
 *
 * Return value: the data, which the caller must free.
 **/
char *
camel_pop3_command_get_additional_data (CamelPop3Store *store, int total, CamelException *ex)
{
	GPtrArray *data;
	char *buf, *p;
	int i, len = 0, status = CAMEL_POP3_OK;
	int pc = 0;

	data = g_ptr_array_new ();
	while (1) {
		if (camel_remote_store_recv_line (CAMEL_REMOTE_STORE (store), &buf, ex) < 0) {
			status = CAMEL_POP3_FAIL;
			break;
		}

		if (!strcmp (buf, "."))
			break;

		g_ptr_array_add (data, buf);
		len += strlen (buf) + 1;

		if (total) {
			pc = (len+1) * 100 / total;
			camel_operation_progress(NULL, pc);
		} else {
			camel_operation_progress_count(NULL, len);
		}
	}
	
	if (buf)
		g_free (buf);

	if (status == CAMEL_POP3_OK) {
		buf = g_malloc0 (len + 1);

		for (i = 0, p = buf; i < data->len; i++) {
			char *ptr, *datap;

			datap = (char *) data->pdata[i];
			ptr = (*datap == '.') ? datap + 1 : datap;
			len = strlen (ptr);
			memcpy (p, ptr, len);
			p += len;
			*p++ = '\n';
		}
		*p = '\0';
	} else
		buf = NULL;

	for (i = 0; i < data->len; i++)
		g_free (data->pdata[i]);
	g_ptr_array_free (data, TRUE);

	return buf;
}

