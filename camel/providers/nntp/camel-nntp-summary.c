/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "camel/camel-file-utils.h"
#include "camel/camel-mime-message.h"
#include "camel/camel-stream-null.h"
#include "camel/camel-operation.h"
#include "camel/camel-data-cache.h"

#include "camel-nntp-summary.h"
#include "camel-nntp-folder.h"
#include "camel-nntp-store.h"
#include "camel-nntp-stream.h"

#define w(x)
#define io(x)
#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/
extern int camel_verbose_debug;
#define dd(x) (camel_verbose_debug?(x):0)

#define CAMEL_NNTP_SUMMARY_VERSION (0x200)

static int xover_setup(CamelNNTPSummary *cns, CamelException *ex);
static int add_range_xover(CamelNNTPSummary *cns, unsigned int high, unsigned int low, CamelFolderChangeInfo *changes, CamelException *ex);
static int add_range_head(CamelNNTPSummary *cns, unsigned int high, unsigned int low, CamelFolderChangeInfo *changes, CamelException *ex);

enum _xover_t {
	XOVER_STRING = 0,
	XOVER_MSGID,
	XOVER_SIZE,
};

struct _xover_header {
	struct _xover_header *next;

	const char *name;
	unsigned int skip:8;
	enum _xover_t type:8;
};

struct _CamelNNTPSummaryPrivate {
	char *uid;

	struct _xover_header *xover; /* xoverview format */
	int xover_setup;
};

#define _PRIVATE(o) (((CamelNNTPSummary *)(o))->priv)

static CamelMessageInfo * message_info_new (CamelFolderSummary *, struct _header_raw *);
static int summary_header_load(CamelFolderSummary *, FILE *);
static int summary_header_save(CamelFolderSummary *, FILE *);

static void camel_nntp_summary_class_init (CamelNNTPSummaryClass *klass);
static void camel_nntp_summary_init       (CamelNNTPSummary *obj);
static void camel_nntp_summary_finalise   (CamelObject *obj);
static CamelFolderSummaryClass *camel_nntp_summary_parent;

CamelType
camel_nntp_summary_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register(camel_folder_summary_get_type(), "CamelNNTPSummary",
					   sizeof (CamelNNTPSummary),
					   sizeof (CamelNNTPSummaryClass),
					   (CamelObjectClassInitFunc) camel_nntp_summary_class_init,
					   NULL,
					   (CamelObjectInitFunc) camel_nntp_summary_init,
					   (CamelObjectFinalizeFunc) camel_nntp_summary_finalise);
	}
	
	return type;
}

static void
camel_nntp_summary_class_init(CamelNNTPSummaryClass *klass)
{
	CamelFolderSummaryClass *sklass = (CamelFolderSummaryClass *) klass;
	
	camel_nntp_summary_parent = CAMEL_FOLDER_SUMMARY_CLASS(camel_type_get_global_classfuncs(camel_folder_summary_get_type()));

	sklass->message_info_new  = message_info_new;
	sklass->summary_header_load = summary_header_load;
	sklass->summary_header_save = summary_header_save;
}

static void
camel_nntp_summary_init(CamelNNTPSummary *obj)
{
	struct _CamelNNTPSummaryPrivate *p;
	struct _CamelFolderSummary *s = (CamelFolderSummary *)obj;

	p = _PRIVATE(obj) = g_malloc0(sizeof(*p));

	/* subclasses need to set the right instance data sizes */
	s->message_info_size = sizeof(CamelMessageInfo);
	s->content_info_size = sizeof(CamelMessageContentInfo);

	/* and a unique file version */
	s->version += CAMEL_NNTP_SUMMARY_VERSION;
}

static void
camel_nntp_summary_finalise(CamelObject *obj)
{
	CamelNNTPSummary *cns = CAMEL_NNTP_SUMMARY(obj);
	struct _xover_header *xover, *xn;

	xover = cns->priv->xover;
	while (xover) {
		xn = xover->next;
		g_free(xover);
		xover = xn;
	}

	g_free(cns->priv);
}

CamelNNTPSummary *
camel_nntp_summary_new(CamelNNTPFolder *folder)
{
	CamelNNTPSummary *cns = (CamelNNTPSummary *)camel_object_new(camel_nntp_summary_get_type());
	char *path;

	cns->folder = folder;
	path = g_strdup_printf("%s.ev-summary", folder->storage_path);
	camel_folder_summary_set_filename((CamelFolderSummary *)cns, path);
	g_free(path);

	camel_folder_summary_set_build_content((CamelFolderSummary *)cns, FALSE);

	return cns;
}

static CamelMessageInfo *
message_info_new(CamelFolderSummary *s, struct _header_raw *h)
{
	CamelMessageInfo *mi;
	CamelNNTPSummary *cns = (CamelNNTPSummary *)s;

	/* error to call without this setup */
	if (cns->priv->uid == NULL)
		return NULL;

	/* we shouldn't be here if we already have this uid */
	g_assert(camel_folder_summary_uid(s, cns->priv->uid) == NULL);

	mi = ((CamelFolderSummaryClass *)camel_nntp_summary_parent)->message_info_new(s, h);
	if (mi) {
		camel_message_info_set_uid(mi, cns->priv->uid);
		cns->priv->uid = NULL;
	}
	
	return mi;
}

static int summary_header_load(CamelFolderSummary *s, FILE *in)
{
	CamelNNTPSummary *cns = CAMEL_NNTP_SUMMARY(s);

	if (((CamelFolderSummaryClass *)camel_nntp_summary_parent)->summary_header_load(s, in) == -1
	    || camel_file_util_decode_fixed_int32(in, &cns->high) == -1
	    || camel_file_util_decode_fixed_int32(in, &cns->low) == -1)
		return -1;

	return 0;
}

static int summary_header_save(CamelFolderSummary *s, FILE *out)
{
	CamelNNTPSummary *cns = CAMEL_NNTP_SUMMARY(s);

	if (((CamelFolderSummaryClass *)camel_nntp_summary_parent)->summary_header_save(s, out) == -1
	    || camel_file_util_encode_fixed_int32(out, cns->high) == -1
	    || camel_file_util_encode_fixed_int32(out, cns->low) == -1)
		return -1;

	return 0;
}

/* Assumes we have the stream */
int camel_nntp_summary_check(CamelNNTPSummary *cns, CamelFolderChangeInfo *changes, CamelException *ex)
{
	CamelNNTPStore *store;
	CamelFolder *folder;
	CamelFolderSummary *s;
	int ret, i;
	char *line;
	unsigned int n, f, l;
	int count;

	if (xover_setup(cns, ex) == -1)
		return -1;

	folder = (CamelFolder *)cns->folder;
	store = (CamelNNTPStore *)folder->parent_store;
	s = (CamelFolderSummary *)cns;

	ret = camel_nntp_command(store, &line, "group %s", folder->full_name);
	if (ret == 411) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID,
				     _("No such folder: %s"), line);
		return -1;
	} else if (ret != 211) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				     _("Could not get group: %s"), line);
		return -1;
	}

	line +=3;
	n = strtoul(line, &line, 10);
	f = strtoul(line, &line, 10);
	l = strtoul(line, &line, 10);

	dd(printf("nntp_summary: got last '%u' first '%u'\n"
		  "nntp_summary: high '%u' low '%u'\n", l, f, cns->high, cns->low));

	if (cns->low == f && cns->high == l) {
		dd(printf("nntp_summary: no work to do!\n"));
		return 0;
	}

	/* Need to work out what to do with our messages */

	/* Check for messages no longer on the server */
	if (cns->low != f) {
		count = camel_folder_summary_count(s);
		for (i = 0; i < count; i++) {
			CamelMessageInfo *mi = camel_folder_summary_index(s, i);

			if (mi) {
				const char *uid = camel_message_info_uid(mi);
				const char *msgid;

				n = strtoul(uid, NULL, 10);
				if (n < f || n > l) {
					dd(printf("nntp_summary: %u is lower/higher than lowest/highest article, removed\n", n));
					/* Since we use a global cache this could prematurely remove
					   a cached message that might be in another folder - not that important as
					   it is a true cache */
					msgid = strchr(uid, ',');
					if (msgid)
						camel_data_cache_remove(store->cache, "cache", msgid+1, NULL);
					camel_folder_change_info_remove_uid(changes, uid);
					camel_folder_summary_remove(s, mi);
					count--;
					i--;
				}
				
				camel_folder_summary_info_free(s, mi);
			}
		}
		cns->low = f;
	}

	if (cns->high < l) {
		if (cns->high < f)
			cns->high = f-1;

		if (cns->priv->xover) {
			ret = add_range_xover(cns, l, cns->high+1, changes, ex);
		} else {
			ret = add_range_head(cns, l, cns->high+1, changes, ex);
		}
	}

	camel_folder_summary_touch(s);

	return ret;
}

static struct {
	const char *name;
	int type;
} headers[] = {
	{ "subject", 0 },
	{ "from", 0 },
	{ "date", 0 },
	{ "message-id", 1 },
	{ "references", 0 },
	{ "bytes", 2 },
};

static int
xover_setup(CamelNNTPSummary *cns, CamelException *ex)
{
	CamelNNTPStore *store;
	CamelFolder *folder;
	CamelFolderSummary *s;
	int ret, i;
	char *line;
	unsigned int len;
	unsigned char c, *p;
	struct _xover_header *xover, *last;

	if (cns->priv->xover_setup)
		return 0;

	/* manual override */
	if (getenv("CAMEL_NNTP_DISABLE_XOVER") != NULL) {
		cns->priv->xover_setup = TRUE;
		return 0;
	}

	folder = (CamelFolder *)cns->folder;
	store = (CamelNNTPStore *)folder->parent_store;
	s = (CamelFolderSummary *)cns;

	ret = camel_nntp_command(store, &line, "list overview.fmt");
	if (ret == -1) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
				     _("NNTP Command failed: %s"), strerror(errno));
		return -1;
	}

	cns->priv->xover_setup = TRUE;

	/* unsupported command? */
	if (ret != 215)
		return 0;

	last = (struct _xover_header *)&cns->priv->xover;

	/* supported command */
	while ((ret = camel_nntp_stream_line(store->stream, (unsigned char **)&line, &len)) > 0) {
		p = line;
		xover = g_malloc0(sizeof(*xover));
		last->next = xover;
		last = xover;
		while ((c = *p++)) {
			if (c == ':') {
				p[-1] = 0;
				for (i=0;i<sizeof(headers)/sizeof(headers[0]);i++) {
					if (strcmp(line, headers[i].name) == 0) {
						xover->name = headers[i].name;
						if (strncmp(p, "full", 4) == 0)
							xover->skip = strlen(xover->name)+1;
						else
							xover->skip = 0;
						xover->type = headers[i].type;
						break;
					}
				}
				break;
			} else {
				p[-1] = tolower(c);
			}
		}
	}

	return ret;
}

static int
add_range_xover(CamelNNTPSummary *cns, unsigned int high, unsigned int low, CamelFolderChangeInfo *changes, CamelException *ex)
{
	CamelNNTPStore *store;
	CamelFolder *folder;
	CamelFolderSummary *s;
	CamelMessageInfo *mi;
	struct _header_raw *headers = NULL;
	char *line, *tab;
	int len, ret;
	unsigned int n, count, total, size;
	struct _xover_header *xover;
	time_t last, now;

	folder = (CamelFolder *)cns->folder;
	store = (CamelNNTPStore *)folder->parent_store;
	s = (CamelFolderSummary *)cns;

	camel_operation_start(NULL, _("%s: Scanning new messages"), ((CamelService *)store)->url->host);

	ret = camel_nntp_command(store, &line, "xover %r", low, high);
	if (ret != 224) {
		camel_operation_end(NULL);
		return -1;
	}

	last = time(0);
	count = 0;
	total = high-low+1;
	while ((ret = camel_nntp_stream_line(store->stream, (unsigned char **)&line, &len)) > 0) {
		camel_operation_progress(NULL, (count * 100) / total);
		count++;
		n = strtoul(line, &tab, 10);
		if (*tab != '\t')
			continue;
		tab++;
		xover = cns->priv->xover;
		size = 0;
		for (;tab[0] && xover;xover = xover->next) {
			line = tab;
			tab = strchr(line, '\t');
			if (tab)
				*tab++ = 0;
			else
				tab = line+strlen(line);

			/* do we care about this column? */
			if (xover->name) {
				line += xover->skip;
				if (line < tab) {
					header_raw_append(&headers, xover->name, line, -1);
					switch(xover->type) {
					case XOVER_STRING:
						break;
					case XOVER_MSGID:
						cns->priv->uid = g_strdup_printf("%u,%s", n, line);
						break;
					case XOVER_SIZE:
						size = strtoul(line, NULL, 10);
						break;
					}
				}
			}
		}

		/* truncated line? ignore? */
		if (xover == NULL) {
			mi = camel_folder_summary_uid(s, cns->priv->uid);
			if (mi == NULL) {
				mi = camel_folder_summary_add_from_header(s, headers);
				if (mi) {
					mi->size = size;
					cns->high = n;
					camel_folder_change_info_add_uid(changes, camel_message_info_uid(mi));
				}
			} else {
				camel_folder_summary_info_free(s, mi);
			}
		}

		if (cns->priv->uid) {
			g_free(cns->priv->uid);
			cns->priv->uid = NULL;
		}

		header_raw_clear(&headers);

		now = time(0);
		if (last + 2 < now) {
			camel_object_trigger_event((CamelObject *)folder, "folder_changed", changes);
			camel_folder_change_info_clear(changes);
			last = now;
		}
	}

	camel_operation_end(NULL);

	return ret;
}

static int
add_range_head(CamelNNTPSummary *cns, unsigned int high, unsigned int low, CamelFolderChangeInfo *changes, CamelException *ex)
{
	CamelNNTPStore *store;
	CamelFolder *folder;
	CamelFolderSummary *s;
	int i, ret = -1;
	char *line, *msgid;
	unsigned int n, count, total;
	CamelMessageInfo *mi;
	CamelMimeParser *mp;
	time_t now, last;

	folder = (CamelFolder *)cns->folder;
	store = (CamelNNTPStore *)folder->parent_store;
	s = (CamelFolderSummary *)cns;

	mp = camel_mime_parser_new();

	camel_operation_start(NULL, _("%s: Scanning new messages"), ((CamelService *)store)->url->host);

	last = time(0);
	count = 0;
	total = high-low+1;
	for (i=low;i<high+1;i++) {
		camel_operation_progress(NULL, (count * 100) / total);
		count++;
		ret = camel_nntp_command(store, &line, "head %u", i);
		/* unknown article, ignore */
		if (ret == 423)
			continue;
		else if (ret == -1)
			goto error;
		else if (ret != 221) {
			camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, _("Unknown server response: %s"), line);
			goto ioerror;
		}
		line += 3;
		n = strtoul(line, &line, 10);
		if (n != i)
			g_warning("retrieved message '%d' when i expected '%d'?\n", n, i);

		if ((msgid = strchr(line, '<')) && (line = strchr(msgid+1, '>'))){
			line[1] = 0;
			cns->priv->uid = g_strdup_printf("%u,%s\n", n, msgid);
			mi = camel_folder_summary_uid(s, cns->priv->uid);
			if (mi == NULL) {
				if (camel_mime_parser_init_with_stream(mp, (CamelStream *)store->stream) == -1)
					goto error;
				mi = camel_folder_summary_add_from_parser(s, mp);
				while (camel_mime_parser_step(mp, NULL, NULL) != HSCAN_EOF)
					;
				if (mi == NULL) {
					goto error;
				}
				cns->high = i;
				camel_folder_change_info_add_uid(changes, camel_message_info_uid(mi));
			} else {
				/* already have, ignore */
				camel_folder_summary_info_free(s, mi);
			}
			if (cns->priv->uid) {
				g_free(cns->priv->uid);
				cns->priv->uid = NULL;
			}
		}

		now = time(0);
		if (last + 2 < now) {
			camel_object_trigger_event((CamelObject *)folder, "folder_changed", changes);
			camel_folder_change_info_clear(changes);
			last = now;
		}
	}

	ret = 0;
error:

	if (ret == -1) {
		if (errno == EINTR)
			camel_exception_setv(ex, CAMEL_EXCEPTION_USER_CANCEL, _("Use cancel"));
		else
			camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, _("Operation failed: %s"), strerror(errno));
	}
ioerror:

	if (cns->priv->uid) {
		g_free(cns->priv->uid);
		cns->priv->uid = NULL;
	}
	camel_object_unref((CamelObject *)mp);

	camel_operation_end(NULL);

	return ret;
}
