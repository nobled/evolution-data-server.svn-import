/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * e-passwords.c
 *
 * Copyright (C) 2001 Ximian, Inc.
 */

/*
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
 * USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e-passwords.h"
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-config.h>
#include <libgnomeui/gnome-dialog.h>
#include <libgnomeui/gnome-messagebox.h>
#include <libgnomeui/gnome-stock.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkcheckbutton.h>

static char *decode_base64 (char *base64);

static GHashTable *passwords = NULL;

static int base64_encode_close(unsigned char *in, int inlen, gboolean break_lines, unsigned char *out, int *state, int *save);
static int base64_encode_step(unsigned char *in, int len, gboolean break_lines, unsigned char *out, int *state, int *save);

void
e_passwords_init ()
{
	char *key, *value;
	void *iter;

	passwords = g_hash_table_new (g_str_hash, g_str_equal);

	iter = gnome_config_private_init_iterator ("/Evolution/Passwords");
	if (iter) {
		while (gnome_config_iterator_next (iter, &key, &value)) {
			g_hash_table_insert (passwords,
					     decode_base64 (key),
					     decode_base64 (value));
			g_free (key);
			g_free (value);
		}
	}
}

static gboolean
free_entry (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	memset (value, 0, strlen (value));
	g_free (value);
	return TRUE;
}

void
e_passwords_shutdown ()
{
	gnome_config_sync ();
	g_hash_table_foreach_remove (passwords, free_entry, NULL);
	g_hash_table_destroy (passwords);
	passwords = NULL;
}


void
e_passwords_forget_passwords ()
{
	g_hash_table_foreach_remove (passwords, free_entry, NULL);
	gnome_config_private_clean_section ("/Evolution/Passwords");
	gnome_config_sync ();
}

static void
maybe_remember_password (gpointer key, gpointer password, gpointer url)
{
	char *path, *key64, *pass64;
	int len, state, save;
	
	len = strlen (url);
	if (strncmp (key, url, len) != 0)
		return;
	
	len = strlen (key);
	key64 = g_malloc0 ((len + 2) * 4 / 3 + 1);
	state = save = 0;
	base64_encode_close (key, len, FALSE, key64, &state, &save);
	path = g_strdup_printf ("/Evolution/Passwords/%s", key64);
	g_free (key64);
	
	len = strlen (password);
	pass64 = g_malloc0 ((len + 2) * 4 / 3 + 1);
	state = save = 0;
	base64_encode_close (password, len, FALSE, pass64, &state, &save);
	
	gnome_config_private_set_string (path, pass64);
	g_free (path);
	g_free (pass64);
}

void
e_passwords_remember_password (const char *url)
{
	g_hash_table_foreach (passwords, maybe_remember_password, (gpointer)url);
}

void
e_passwords_forget_password (const char *key)
{
	gpointer okey, value;
	
	if (g_hash_table_lookup_extended (passwords, key, &okey, &value)) {
		g_hash_table_remove (passwords, key);
		memset (value, 0, strlen (value));
		g_free (okey);
		g_free (value);
	}
}

const char *
e_passwords_get_password (const char *key)
{
	return g_hash_table_lookup (passwords, key);
}

void
e_passwords_add_password (const char *key, const char *passwd)
{
	g_hash_table_insert (passwords, (gpointer)key, (gpointer)passwd);
}


/**
 * e_passwords_ask_password:
 * @title: title for the password dialog
 * @key: key to store the password under
 * @prompt: prompt string
 * @secret: whether or not the password text should be ***ed out
 * @remember_type: whether or not to offer to remember the password,
 * and for how long.
 * @remember: on input, the default state of the remember checkbox.
 * on output, the state of the checkbox when the dialog was closed.
 * @parent: parent window of the dialog, or %NULL
 *
 * Asks the user for a password.
 *
 * Return value: the password, or %NULL if the user cancelled the
 * operation. *@remember will be set if the return value is non-%NULL
 * and @remember_type is not E_PASSWORDS_DO_NOT_REMEMBER.
 **/
char *
e_passwords_ask_password (const char *title, const char *key,
			  const char *prompt, gboolean secret,
			  EPasswordsRememberType remember_type,
			  gboolean *remember,
			  GtkWindow *parent)
{
	GtkWidget *dialog;
	GtkWidget *check, *entry;
	char *password;
	int button;

	dialog = gnome_message_box_new (prompt, GNOME_MESSAGE_BOX_QUESTION,
					GNOME_STOCK_BUTTON_OK, 
					GNOME_STOCK_BUTTON_CANCEL,
					NULL);
	gtk_window_set_title (GTK_WINDOW (dialog), title);
	if (parent)
		gnome_dialog_set_parent (GNOME_DIALOG (dialog), parent);
	gnome_dialog_set_default (GNOME_DIALOG (dialog), 0);
	gnome_dialog_set_close (GNOME_DIALOG (dialog), FALSE);

	/* Password entry */
	entry = gtk_entry_new();
	if (secret)
		gtk_entry_set_visibility (GTK_ENTRY(entry), FALSE);

	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), 
			    entry, FALSE, FALSE, 4);
	gtk_widget_show (entry);
	gtk_widget_grab_focus (entry);

	/* If Return is pressed in the text entry, propagate to the buttons */
	gnome_dialog_editable_enters (GNOME_DIALOG(dialog), GTK_EDITABLE(entry));

	/* Remember the password? */
	if (remember_type != E_PASSWORDS_DO_NOT_REMEMBER) {
		const char *label;

		if (remember_type == E_PASSWORDS_REMEMBER_FOREVER)
			label = _("Remember this password");
		else
			label = _("Remember this password for the remainder of this session");
		check = gtk_check_button_new_with_label (label);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
					      *remember);

		gtk_box_pack_end (GTK_BOX (GNOME_DIALOG (dialog)->vbox),
				  check, TRUE, FALSE, 4);
		gtk_widget_show (check);
	}

	gtk_widget_show (dialog);
	button = gnome_dialog_run (GNOME_DIALOG (dialog));

	if (button == 0) {
		password = gtk_editable_get_chars (GTK_EDITABLE (entry), 0, -1);
		*remember = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check));

		if (*remember || remember_type == E_PASSWORDS_REMEMBER_FOREVER)
			e_passwords_add_password (key, password);
		if (*remember && remember_type == E_PASSWORDS_REMEMBER_FOREVER)
			e_passwords_remember_password (key);
	} else
		password = NULL;

	gnome_dialog_close (GNOME_DIALOG (dialog));
	return password;
}



static char *base64_alphabet =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static unsigned char camel_mime_base64_rank[256] = {
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255, 62,255,255,255, 63,
	 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,255,255,255,  0,255,255,
	255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
	 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,255,255,255,255,255,
	255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
};

/* call this when finished encoding everything, to
   flush off the last little bit */
static int
base64_encode_close(unsigned char *in, int inlen, gboolean break_lines, unsigned char *out, int *state, int *save)
{
	int c1, c2;
	unsigned char *outptr = out;

	if (inlen>0)
		outptr += base64_encode_step(in, inlen, break_lines, outptr, state, save);

	c1 = ((unsigned char *)save)[1];
	c2 = ((unsigned char *)save)[2];
	
	switch (((char *)save)[0]) {
	case 2:
		outptr[2] = base64_alphabet[ ( (c2 &0x0f) << 2 ) ];
		g_assert(outptr[2] != 0);
		goto skip;
	case 1:
		outptr[2] = '=';
	skip:
		outptr[0] = base64_alphabet[ c1 >> 2 ];
		outptr[1] = base64_alphabet[ c2 >> 4 | ( (c1&0x3) << 4 )];
		outptr[3] = '=';
		outptr += 4;
		break;
	}
	if (break_lines)
		*outptr++ = '\n';

	*save = 0;
	*state = 0;

	return outptr-out;
}

/*
  performs an 'encode step', only encodes blocks of 3 characters to the
  output at a time, saves left-over state in state and save (initialise to
  0 on first invocation).
*/
static int
base64_encode_step(unsigned char *in, int len, gboolean break_lines, unsigned char *out, int *state, int *save)
{
	register unsigned char *inptr, *outptr;

	if (len<=0)
		return 0;

	inptr = in;
	outptr = out;

	if (len + ((char *)save)[0] > 2) {
		unsigned char *inend = in+len-2;
		register int c1, c2, c3;
		register int already;

		already = *state;

		switch (((char *)save)[0]) {
		case 1:	c1 = ((unsigned char *)save)[1]; goto skip1;
		case 2:	c1 = ((unsigned char *)save)[1];
			c2 = ((unsigned char *)save)[2]; goto skip2;
		}
		
		/* yes, we jump into the loop, no i'm not going to change it, it's beautiful! */
		while (inptr < inend) {
			c1 = *inptr++;
		skip1:
			c2 = *inptr++;
		skip2:
			c3 = *inptr++;
			*outptr++ = base64_alphabet[ c1 >> 2 ];
			*outptr++ = base64_alphabet[ c2 >> 4 | ( (c1&0x3) << 4 ) ];
			*outptr++ = base64_alphabet[ ( (c2 &0x0f) << 2 ) | (c3 >> 6) ];
			*outptr++ = base64_alphabet[ c3 & 0x3f ];
			/* this is a bit ugly ... */
			if (break_lines && (++already)>=19) {
				*outptr++='\n';
				already = 0;
			}
		}

		((char *)save)[0] = 0;
		len = 2-(inptr-inend);
		*state = already;
	}

	if (len>0) {
		register char *saveout;

		/* points to the slot for the next char to save */
		saveout = & (((char *)save)[1]) + ((char *)save)[0];

		/* len can only be 0 1 or 2 */
		switch(len) {
		case 2:	*saveout++ = *inptr++;
		case 1:	*saveout++ = *inptr++;
		}
		((char *)save)[0]+=len;
	}

	return outptr-out;
}


/**
 * base64_decode_step: decode a chunk of base64 encoded data
 * @in: input stream
 * @len: max length of data to decode
 * @out: output stream
 * @state: holds the number of bits that are stored in @save
 * @save: leftover bits that have not yet been decoded
 *
 * Decodes a chunk of base64 encoded data
 **/
static int
base64_decode_step(unsigned char *in, int len, unsigned char *out, int *state, unsigned int *save)
{
	register unsigned char *inptr, *outptr;
	unsigned char *inend, c;
	register unsigned int v;
	int i;

	inend = in+len;
	outptr = out;

	/* convert 4 base64 bytes to 3 normal bytes */
	v=*save;
	i=*state;
	inptr = in;
	while (inptr<inend) {
		c = camel_mime_base64_rank[*inptr++];
		if (c != 0xff) {
			v = (v<<6) | c;
			i++;
			if (i==4) {
				*outptr++ = v>>16;
				*outptr++ = v>>8;
				*outptr++ = v;
				i=0;
			}
		}
	}

	*save = v;
	*state = i;

	/* quick scan back for '=' on the end somewhere */
	/* fortunately we can drop 1 output char for each trailing = (upto 2) */
	i=2;
	while (inptr>in && i) {
		inptr--;
		if (camel_mime_base64_rank[*inptr] != 0xff) {
			if (*inptr == '=')
				outptr--;
			i--;
		}
	}

	/* if i!= 0 then there is a truncation error! */
	return outptr-out;
}

static char *
decode_base64 (char *base64)
{
	char *plain, *pad = "==";
	int len, out, state, save;
	
	len = strlen (base64);
	plain = g_malloc0 (len);
	state = save = 0;
	out = base64_decode_step (base64, len, plain, &state, &save);
	if (len % 4) {
		base64_decode_step (pad, 4 - len % 4, plain + out,
				    &state, &save);
	}
	
	return plain;
}
