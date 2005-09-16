/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2001 Ximian, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __CAMEL_GROUPWISE_UTILS__
#define __CAMEL_GROUPWISE_UTILS__

#include <glib.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-string-utils.h>
#include <e-gw-connection.h>
#include <e-gw-container.h>
#include <e-gw-item.h>

/*Headers for send options*/
#define X_SEND_OPTIONS        "X-gw-send-options"
/*General Options*/
#define X_SEND_OPT_PRIORITY   "X-gw-send-opt-priority"
#define X_REPLY_CONVENIENT    "X-reply-convenient"
#define X_REPLY_WITHIN        "X-reply-within"
#define X_EXPIRE_AFTER        "X-expire-after"
#define X_DELAY_UNTIL         "X-delay-until"

/*Status Tracking Options*/
#define X_TRACK_WHEN            "X-track-when"
#define X_AUTODELETE            "X-auto-delete"
#define X_RETURN_NOTIFY_OPEN    "X-return-notify-open"
#define X_RETURN_NOTIFY_DELETE  "X-return-notify-delete"

/* Folder types for source */
#define RECEIVED  "Mailbox"
#define SENT	  "Sent Items"
#define DRAFT	  ""
#define PERSONAL  "Cabinet"

/*for syncing flags back to server*/
typedef struct {
	guint32 changed;
	guint32 bits;
} flags_diff_t;


/* FIXME: deprecated
   This is used exclusively for the legacy imap cache code.  DO NOT use this in any new code */

typedef gboolean (*EPathFindFoldersCallback) (const char *physical_path,
					      const char *path,
					      gpointer user_data);

char *   e_path_to_physical  (const char *prefix, const char *vpath);

gboolean e_path_find_folders (const char *prefix,
			      EPathFindFoldersCallback callback,
			      gpointer data);

int      e_path_rmdir        (const char *prefix, const char *vpath);


EGwItem *camel_groupwise_util_item_from_message (EGwConnection *cnc, CamelMimeMessage *message, CamelAddress *from, CamelAddress *recipients);
void do_flags_diff (flags_diff_t *diff, guint32 old, guint32 _new);
GSList *add_recipients(GSList *recipient_list, CamelAddress *recipients, int recipient_type);
char *gw_concat ( const char *prefix, const char *suffix);
#endif
