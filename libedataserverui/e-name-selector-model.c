/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-name-selector-model.c - Model for contact selection.
 *
 * Copyright (C) 2004 Novell, Inc.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: Hans Petter Jansson <hpj@novell.com>
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <string.h>
#include <glib.h>
#include <libgnome/gnome-i18n.h>
#include <libedataserver/e-data-server-marshal.h>
#include "e-name-selector-model.h"

typedef struct {
	gchar              *name;
	gchar              *pretty_name;

	EDestinationStore  *destination_store;
}
Section;

static gboolean filter_show_not_in_destinations (EContactStore *contact_store, GtkTreeIter *iter,
						 ENameSelectorModel *name_selector_model);

/* ------------------ *
 * Class/object setup *
 * ------------------ */

/* Signals */

enum {
	SECTION_ADDED,
	SECTION_REMOVED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (ENameSelectorModel, e_name_selector_model, G_TYPE_OBJECT);

static void
e_name_selector_model_init (ENameSelectorModel *name_selector_model)
{
	name_selector_model->sections       = g_array_new (FALSE, FALSE, sizeof (Section));
	name_selector_model->contact_store  = e_contact_store_new ();
	name_selector_model->contact_filter = GTK_TREE_MODEL_FILTER (
		gtk_tree_model_filter_new (GTK_TREE_MODEL (name_selector_model->contact_store), NULL));
	gtk_tree_model_filter_set_visible_func (name_selector_model->contact_filter,
						(GtkTreeModelFilterVisibleFunc) filter_show_not_in_destinations,
						name_selector_model, NULL);
	g_object_unref (name_selector_model->contact_store);

	name_selector_model->destination_uid_hash = NULL;
}

static void
e_name_selector_model_finalize (GObject *object)
{
	ENameSelectorModel *name_selector_model = E_NAME_SELECTOR_MODEL (object);

	/* TODO: Free sections */

	g_object_unref (name_selector_model->contact_filter);

	if (G_OBJECT_CLASS (e_name_selector_model_parent_class)->finalize)
		G_OBJECT_CLASS (e_name_selector_model_parent_class)->finalize (object);
}

static void
e_name_selector_model_class_init (ENameSelectorModelClass *name_selector_model_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (name_selector_model_class);

	object_class->finalize = e_name_selector_model_finalize;

	signals [SECTION_ADDED] =
		g_signal_new ("section-added",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ENameSelectorModelClass, section_added),
			      NULL, NULL,
			      e_data_server_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);

	signals [SECTION_REMOVED] =
		g_signal_new ("section-removed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ENameSelectorModelClass, section_removed),
			      NULL, NULL,
			      e_data_server_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);
}

ENameSelectorModel *
e_name_selector_model_new (void)
{
	return E_NAME_SELECTOR_MODEL (g_object_new (E_TYPE_NAME_SELECTOR_MODEL, NULL));
}

/* ---------------------------- *
 * GtkTreeModelFilter filtering *
 * ---------------------------- */

static gboolean
filter_show_not_in_destinations (EContactStore *contact_store, GtkTreeIter *iter,
				 ENameSelectorModel *name_selector_model)
{
	EContact    *contact;
	const gchar *contact_uid;
	gboolean     result = TRUE;
	gint         i;

	contact = e_contact_store_get_contact (contact_store, iter);
	g_assert (contact != NULL);

	contact_uid = e_contact_get_const (contact, E_CONTACT_UID);
	if (!contact_uid)
		return TRUE;  /* Can happen with broken databases */

	for (i = 0; i < name_selector_model->sections->len && result == TRUE; i++) {
		Section *section;
		GList   *destinations;
		GList   *l;

		section = &g_array_index (name_selector_model->sections, Section, i);
		destinations = e_destination_store_list_destinations (section->destination_store);

		for (l = destinations; l; l = g_list_next (l)) {
			EDestination *destination = l->data;
			const gchar  *destination_uid;

			destination_uid = e_destination_get_contact_uid (destination);
			if (destination_uid && !strcmp (contact_uid, destination_uid)) {
				result = FALSE;
				break;
			}
		}

		g_list_free (destinations);
	}

	return result;
}

/* --------------- *
 * Section helpers *
 * --------------- */

typedef struct
{
	ENameSelectorModel *name_selector_model;
	GHashTable         *other_hash;
}
HashCompare;

static void
emit_destination_uid_changes_cb (const gchar *uid, gpointer value, HashCompare *hash_compare)
{
	EContactStore *contact_store = hash_compare->name_selector_model->contact_store;

	if (!hash_compare->other_hash || !g_hash_table_lookup (hash_compare->other_hash, uid)) {
		GtkTreeIter  iter;
		GtkTreePath *path;

		if (e_contact_store_find_contact (contact_store, uid, &iter)) {
			path = gtk_tree_model_get_path (GTK_TREE_MODEL (contact_store), &iter);
			gtk_tree_model_row_changed (GTK_TREE_MODEL (contact_store), path, &iter);
			gtk_tree_path_free (path);
		}
	}
}

static void
destinations_changed (ENameSelectorModel *name_selector_model)
{
	GHashTable  *destination_uid_hash_new;
	GHashTable  *destination_uid_hash_old;
	HashCompare  hash_compare;
	gint         i;

	destination_uid_hash_new = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	for (i = 0; i < name_selector_model->sections->len; i++) {
		Section *section = &g_array_index (name_selector_model->sections, Section, i);
		GList   *destinations;
		GList   *l;

		destinations = e_destination_store_list_destinations (section->destination_store);

		for (l = destinations; l; l = g_list_next (l)) {
			EDestination *destination = l->data;
			const gchar  *destination_uid;

			destination_uid = e_destination_get_contact_uid (destination);
			if (destination_uid)
				g_hash_table_insert (destination_uid_hash_new,
						     g_strdup (destination_uid),
						     GINT_TO_POINTER (TRUE));
		}

		g_list_free (destinations);
	}

	destination_uid_hash_old = name_selector_model->destination_uid_hash;
	name_selector_model->destination_uid_hash = destination_uid_hash_new;

	hash_compare.name_selector_model = name_selector_model;

	hash_compare.other_hash = destination_uid_hash_old;
	g_hash_table_foreach (destination_uid_hash_new, (GHFunc) emit_destination_uid_changes_cb,
			      &hash_compare);

	if (destination_uid_hash_old) {
		hash_compare.other_hash = destination_uid_hash_new;
		g_hash_table_foreach (destination_uid_hash_old, (GHFunc) emit_destination_uid_changes_cb,
				      &hash_compare);

		g_hash_table_destroy (destination_uid_hash_old);
	}
}

static void
free_section (ENameSelectorModel *name_selector_model, gint n)
{
	Section *section;

	g_assert (n >= 0);
	g_assert (n < name_selector_model->sections->len);

	section = &g_array_index (name_selector_model->sections, Section, n);

	g_signal_handlers_disconnect_matched (section->destination_store, G_SIGNAL_MATCH_DATA,
					      0, 0, NULL, NULL, name_selector_model);

	g_free (section->name);
	g_free (section->pretty_name);
	g_object_unref (section->destination_store);
}

static gint
find_section_by_name (ENameSelectorModel *name_selector_model, const gchar *name)
{
	gint i;

	g_assert (name != NULL);

	for (i = 0; i < name_selector_model->sections->len; i++) {
		Section *section = &g_array_index (name_selector_model->sections, Section, i);

		if (!strcmp (name, section->name))
			return i;
	}

	return -1;
}

/* ---------------------- *
 * ENameSelectorModel API *
 * ---------------------- */

EContactStore *
e_name_selector_model_peek_contact_store (ENameSelectorModel *name_selector_model)
{
	g_return_val_if_fail (E_IS_NAME_SELECTOR_MODEL (name_selector_model), NULL);

	return name_selector_model->contact_store;
}

GtkTreeModelFilter *
e_name_selector_model_peek_contact_filter (ENameSelectorModel *name_selector_model)
{
	g_return_val_if_fail (E_IS_NAME_SELECTOR_MODEL (name_selector_model), NULL);

	return name_selector_model->contact_filter;
}

GList *
e_name_selector_model_list_sections (ENameSelectorModel *name_selector_model)
{
	GList *section_names = NULL;
	gint   i;

	g_return_val_if_fail (E_IS_NAME_SELECTOR_MODEL (name_selector_model), NULL);

	/* Do this backwards so we can use g_list_prepend () and get correct order */
	for (i = name_selector_model->sections->len - 1; i >= 0; i--) {
		Section *section = &g_array_index (name_selector_model->sections, Section, i);
		gchar   *name;

		name = g_strdup (section->name);
		section_names = g_list_prepend (section_names, name);
	}

	return section_names;
}

void
e_name_selector_model_add_section (ENameSelectorModel *name_selector_model,
				   const gchar *name, const gchar *pretty_name,
				   EDestinationStore *destination_store)
{
	Section section;

	g_return_if_fail (E_IS_NAME_SELECTOR_MODEL (name_selector_model));
	g_return_if_fail (name != NULL);
	g_return_if_fail (pretty_name != NULL);

	if (find_section_by_name (name_selector_model, name) >= 0) {
		g_warning ("ENameSelectorModel already has a section called '%s'!", name);
		return;
	}

	memset (&section, 0, sizeof (Section));

	section.name              = g_strdup (name);
	section.pretty_name       = g_strdup (pretty_name);

	if (destination_store)
		section.destination_store = g_object_ref (destination_store);
	else
		section.destination_store = e_destination_store_new ();

	g_signal_connect_swapped (section.destination_store, "row-changed",
				  G_CALLBACK (destinations_changed), name_selector_model);
	g_signal_connect_swapped (section.destination_store, "row-deleted",
				  G_CALLBACK (destinations_changed), name_selector_model);
	g_signal_connect_swapped (section.destination_store, "row-inserted",
				  G_CALLBACK (destinations_changed), name_selector_model);

	g_array_append_val (name_selector_model->sections, section);

	destinations_changed (name_selector_model);
	g_signal_emit (name_selector_model, signals [SECTION_ADDED], 0, name);
}

void
e_name_selector_model_remove_section (ENameSelectorModel *name_selector_model, const gchar *name)
{
	Section *section;
	gint     n;

	g_return_if_fail (E_IS_NAME_SELECTOR_MODEL (name_selector_model));
	g_return_if_fail (name != NULL);

	n = find_section_by_name (name_selector_model, name);
	if (n < 0) {
		g_warning ("ENameSelectorModel does not have a section called '%s'!", name);
		return;
	}

	free_section (name_selector_model, n);
	g_array_remove_index_fast (name_selector_model->sections, n);  /* Order doesn't matter */

	destinations_changed (name_selector_model);
	g_signal_emit (name_selector_model, signals [SECTION_REMOVED], 0, name);
}

gboolean
e_name_selector_model_peek_section (ENameSelectorModel *name_selector_model, const gchar *name,
				    gchar **pretty_name, EDestinationStore **destination_store)
{
	Section *section;
	gint     n;

	g_return_val_if_fail (E_IS_NAME_SELECTOR_MODEL (name_selector_model), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	n = find_section_by_name (name_selector_model, name);
	if (n < 0) {
		g_warning ("ENameSelectorModel does not have a section called '%s'!", name);
		return FALSE;
	}

	section = &g_array_index (name_selector_model->sections, Section, n);

	if (pretty_name)
		*pretty_name = g_strdup (section->pretty_name);
	if (destination_store)
		*destination_store = section->destination_store;

	return TRUE;
}
