/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* 
 * Author : Bertrand Guiheneuf <bertrand@helixcode.com> 
 *
 * Copyright (C) 1999 - 2000 Helix Code .

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

#include "camel-log.h"
#include "camel-exception.h"
#include "camel-mbox-folder.h"
#include "camel-mbox-summary.h"
#include "camel-folder-summary.h"
#include "md5-utils.h"


#include <sys/stat.h> 
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>




/* 
 * The mbox provider uses a summary files, 
 * so that it has an internal and an external
 * summary. The internal summary is a summary
 * containing a lot of information, including
 * infos on how to access mails in the mbox file
 * 
 * On the other hand, the external summary is 
 * an implementation of the structure defined in 
 * the camel-folder-summary file (toplevel camel
 * directory)
 *
 * To sum up, the internal summary is only a
 * subset of the internal summary. 
 */



/**
 * camel_mbox_save_summary:
 * @summary: 
 * @filename: 
 * @ex: 
 * 
 * save the internal summary into a file 
 **/
void 
camel_mbox_save_summary (CamelMboxSummary *summary, const gchar *filename, CamelException *ex)
{
	CamelMboxSummaryInformation *msg_info;
	guint cur_msg;
	guint field_lgth;
	gint fd;
	gint write_result;

	CAMEL_LOG_FULL_DEBUG ("CamelMboxFolder::save_summary entering \n");

	fd = open (filename, 
		   O_WRONLY | O_CREAT | O_TRUNC,
		   S_IRUSR  | S_IWUSR);
	if (fd == -1) {
			camel_exception_setv (ex, 
					     CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION,
					     "could not create the mbox summary file\n"
					      "\t%s\n"
					      "Full error is : %s\n",
					      filename,
					      strerror (errno));
			return;
		}
	
	/* compute and write the mbox file md5 signature */
	//md5_get_digest_from_file (filename, summary->md5_digest);

	/* write the number of messages  + the md5 signatures 
	 + next UID + mbox file size */
	write_result = write (fd, summary, G_STRUCT_OFFSET (CamelMboxSummary, message_info));

	
	for (cur_msg=0; cur_msg < summary->nb_message; cur_msg++) {

		msg_info = (CamelMboxSummaryInformation *)(summary->message_info->data) + cur_msg;

		/* write message position + message size 
		   + x-evolution offset + uid + status */
		write (fd, (gchar *)msg_info, 
		       sizeof (guint32) + 2 * sizeof (guint) + 
		       sizeof (guint32) + sizeof (guchar));
		
		/* write subject */
		field_lgth = msg_info->subject ? strlen (msg_info->subject) : 0;
		write (fd, &field_lgth, sizeof (guint));
		if (field_lgth)
			write (fd, msg_info->subject, field_lgth);
		/* write sender */
		field_lgth = msg_info->sender ? strlen (msg_info->sender) : 0;
		write (fd, &field_lgth, sizeof (gint));
		if (field_lgth)
			write (fd, msg_info->sender, field_lgth);

		/* write to */
		field_lgth = msg_info->to ? strlen (msg_info->to) : 0;
		write (fd, &field_lgth, sizeof (gint));
		if (field_lgth)
			write (fd, msg_info->to, field_lgth);

		/* write date */
		field_lgth = msg_info->date ? strlen (msg_info->date) : 0;
		write (fd, &field_lgth, sizeof (guint));
		if (field_lgth)
			write (fd, msg_info->date, field_lgth);


	}
			
	close (fd);

	CAMEL_LOG_FULL_DEBUG ("CamelMboxFolder::save_summary leaving \n");
}





/**
 * camel_mbox_load_summary:
 * @filename: 
 * @ex: 
 * 
 * load the internal summary from a file 
 * 
 * Return value: 
 **/
CamelMboxSummary *
camel_mbox_load_summary (const gchar *filename, CamelException *ex)
{
	CamelMboxSummaryInformation *msg_info;
	guint cur_msg;
	guint field_lgth;
	gint fd;
	CamelMboxSummary *summary;
	gint read_result;

	CAMEL_LOG_FULL_DEBUG ("CamelMboxFolder::save_summary entering \n");

	fd = open (filename, O_RDONLY);
	if (fd == -1) {
			camel_exception_setv (ex, 
					     CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION,
					     "could not open the mbox summary file\n"
					      "\t%s\n"
					      "Full error is : %s\n",
					      filename,
					      strerror (errno));
			return NULL;
		}
	summary = g_new0 (CamelMboxSummary, 1);

	/* read the message number, the md5 signature 
	 and the next available UID + mbox file size */
	read_result = read (fd, summary, G_STRUCT_OFFSET (CamelMboxSummary, message_info));


	summary->message_info = g_array_new (FALSE, FALSE, sizeof (CamelMboxSummaryInformation));
	summary->message_info =  g_array_set_size (summary->message_info, summary->nb_message);

	
	for (cur_msg=0; cur_msg < summary->nb_message; cur_msg++)  {
		
		msg_info = (CamelMboxSummaryInformation *)(summary->message_info->data) + cur_msg;
		
		/* read message position  + message size 
		   + x-evolution offset + uid + status */
		read (fd, (gchar *)msg_info, 
		       sizeof (guint32) + 2 * sizeof (guint) + 
		       sizeof (guint32) + sizeof (guchar));
		

		/* read the subject */
		read (fd, &field_lgth, sizeof (gint));
		if (field_lgth > 0) {			
			msg_info->subject = g_new0 (gchar, field_lgth + 1);
			read (fd, msg_info->subject, field_lgth);
		} else 
			msg_info->subject = NULL;
		
		/* read the sender */
		read (fd, &field_lgth, sizeof (gint));
		if (field_lgth > 0) {			
			msg_info->sender = g_new0 (gchar, field_lgth + 1);
			read (fd, msg_info->sender, field_lgth);
		} else 
			msg_info->sender = NULL;
		
		/* read the "to" field */
		read (fd, &field_lgth, sizeof (gint));
		if (field_lgth > 0) {			
			msg_info->to = g_new0 (gchar, field_lgth + 1);
			read (fd, msg_info->to, field_lgth);
		} else 
			msg_info->to = NULL;
		
		/* read the "date" field */
		read (fd, &field_lgth, sizeof (gint));
		if (field_lgth > 0) {			
			msg_info->date = g_new0 (gchar, field_lgth + 1);
			read (fd, msg_info->date, field_lgth);
		} else 
			msg_info->date = NULL;
		

		
		
	}		
	
	close (fd);
	return summary;
}









/**
 * camel_mbox_check_summary_sync:
 * @summary_filename: 
 * @mbox_filename: 
 * @ex: 
 * 
 * check if the summary file is in sync with the mbox file
 * 
 * Return value: 
 **/
gboolean
camel_mbox_check_summary_sync (gchar *summary_filename,
			       gchar *mbox_filename,
			       CamelException *ex)

{
	gint fd;
	guchar summary_md5[16];
	guchar real_md5[16];

	
	CAMEL_LOG_FULL_DEBUG ("CamelMboxFolder::save_summary entering \n");

	fd = open (summary_filename, O_RDONLY);
	if (fd == -1) {
			camel_exception_setv (ex, 
					     CAMEL_EXCEPTION_FOLDER_INSUFFICIENT_PERMISSION,
					     "could not open the mbox summary file\n"
					      "\t%s\n"
					      "Full error is : %s\n",
					      summary_filename,
					      strerror (errno));
			return FALSE;
		}
	
	/* skip the message number field */
	lseek (fd, sizeof (guint), SEEK_SET);
	
	/* read the md5 signature stored in the summary file */
	read (fd, summary_md5, sizeof (guchar) * 16);
	close (fd);
	/* ** FIXME : check for exception in all these operations */
	
	/* compute the actual md5 signature on the 
	   mbox file */
	md5_get_digest_from_file (mbox_filename, real_md5);
	
	return (strncmp (real_md5, summary_md5, 16) == 0);
}





/**
 * camel_mbox_summary_append_entries:
 * @summary: 
 * @entries: 
 * 
 * append an entry to an internal summary
 **/
void
camel_mbox_summary_append_entries (CamelMboxSummary *summary, GArray *entries)
{
		
	summary->message_info = g_array_append_vals (summary->message_info, entries->data, entries->len);
	
}





/**
 * camel_mbox_summary_append_internal_to_external:
 * @internal: 
 * @external: 
 * @first_entry: first entry to append.
 * 
 * append some entries from the internal summary to 
 * the external one.
 **/
void 
camel_mbox_summary_append_internal_to_external (CamelMboxSummary *internal, 
						CamelFolderSummary *external, 
						guint first_entry)
{
	GArray *internal_array;
	GArray *external_array;
	
	CamelMessageInfo external_entry;
	CamelMboxSummaryInformation *internal_entry;
	
	int i;

	
	internal_array = internal->message_info;
	external_array = external->message_info_list;
	
	/* we don't set any extra fields */
	external_entry.extended_fields = NULL;


	for (i=first_entry; i<internal_array->len; i++) {
		internal_entry = (CamelMboxSummaryInformation *)(internal_array->data) + i;
		
		external_entry.subject = internal_entry->subject ? strdup (internal_entry->subject) : NULL;
		external_entry.uid = g_strdup_printf ("%u", internal_entry->uid);
		external_entry.sent_date = internal_entry->date ? strdup (internal_entry->date) : NULL;
		external_entry.sender = internal_entry->sender ? strdup (internal_entry->sender) : NULL;

		g_array_append_vals (external_array, &external_entry, 1);
		
	}
	
	
}

