/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source-option-menu.c
 *
 * Copyright (C) 2007 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-source-combo-box.h"

#define E_SOURCE_COMBO_BOX_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE_COMBO_BOX, ESourceComboBoxPrivate))

struct _ESourceComboBoxPrivate {
	ESourceList *source_list;
	GHashTable *uid_index;
	gulong handler_id;
};

enum {
	PROP_0,
	PROP_SOURCE_LIST
};

enum {
	COLUMN_NAME,		/* G_TYPE_STRING */
	COLUMN_SENSITIVE,	/* G_TYPE_BOOLEAN */
	COLUMN_SOURCE,		/* G_TYPE_OBJECT */
	NUM_COLUMNS
};

static gpointer parent_class = NULL;

static void
source_list_changed_cb (ESourceList *source_list,
                        ESourceComboBox *source_combo_box)
{
	ESourceComboBoxPrivate *priv;
	GtkComboBox *combo_box;
	GtkTreeModel *model;
	GtkListStore *store;
	GtkTreeIter iter;
	GtkTreePath *path;
	GSList *groups;
	GSList *sources;
	const gchar *name;
	const gchar *uid;
	gchar *indented_name;

	priv = source_combo_box->priv;
	g_hash_table_remove_all (priv->uid_index);

	combo_box = GTK_COMBO_BOX (source_combo_box);
	gtk_combo_box_set_active (combo_box, -1);

	model = gtk_combo_box_get_model (combo_box);
	store = GTK_LIST_STORE (model);
	gtk_list_store_clear (store);

	for (groups = e_source_list_peek_groups (source_list);
		groups != NULL; groups = groups->next) {

		/* Only show source groups that have sources. */
		if (e_source_group_peek_sources (groups->data) == NULL)
			continue;

		name = e_source_group_peek_name (groups->data);
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (
			store, &iter,
			COLUMN_NAME, name,
			COLUMN_SENSITIVE, FALSE,
			COLUMN_SOURCE, groups->data,
			-1);

		for (sources = e_source_group_peek_sources (groups->data);
			sources != NULL; sources = sources->next) {

			name = e_source_peek_name (sources->data);
			indented_name = g_strconcat ("    ", name, NULL);
			gtk_list_store_append (store, &iter);
			gtk_list_store_set (
				store, &iter,
				COLUMN_NAME, indented_name,
				COLUMN_SENSITIVE, TRUE,
				COLUMN_SOURCE, sources->data,
				-1);
			g_free (indented_name);

			uid = e_source_peek_uid (sources->data);
			path = gtk_tree_model_get_path (model, &iter);
			g_hash_table_insert (
				priv->uid_index, g_strdup (uid),
				gtk_tree_row_reference_new (model, path));
			gtk_tree_path_free (path);
		}
	}
}

static GObject *
e_source_combo_box_constructor (GType type, guint n_construct_properties,
                                GObjectConstructParam *construct_properties)
{
	GtkCellRenderer *renderer;
	GtkListStore *store;
	GObject *object;

	/* Chain up to parent's "constructor" method. */
	object = G_OBJECT_CLASS (parent_class)->constructor (
		type, n_construct_properties, construct_properties);

	store = gtk_list_store_new (
		NUM_COLUMNS, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_OBJECT);
	gtk_combo_box_set_model (
		GTK_COMBO_BOX (object), GTK_TREE_MODEL (store));

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (
		GTK_CELL_LAYOUT (object), renderer, TRUE);
	gtk_cell_layout_set_attributes (
		GTK_CELL_LAYOUT (object), renderer,
		"text", COLUMN_NAME,
		"sensitive", COLUMN_SENSITIVE,
		NULL);

	return object;
}

static void
e_source_combo_box_set_property (GObject *object, guint property_id,
                                 const GValue *value, GParamSpec *pspec)
{
	ESourceComboBoxPrivate *priv;

	priv = E_SOURCE_COMBO_BOX_GET_PRIVATE (object);

	switch (property_id) {
		case PROP_SOURCE_LIST:

			if (priv->source_list != NULL) {
				g_signal_handler_disconnect (
					priv->source_list, priv->handler_id);
				g_object_unref (priv->source_list);
			}

			priv->source_list = g_value_dup_object (value);

			/* Reset the tree store. */
			source_list_changed_cb (
				priv->source_list,
				E_SOURCE_COMBO_BOX (object));

			/* Watch for source list changes. */
			priv->handler_id = g_signal_connect_object (
				priv->source_list, "changed",
				G_CALLBACK (source_list_changed_cb),
				object, 0);

			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_source_combo_box_get_property (GObject *object, guint property_id,
                                 GValue *value, GParamSpec *pspec)
{
	ESourceComboBoxPrivate *priv;

	priv = E_SOURCE_COMBO_BOX_GET_PRIVATE (object);

	switch (property_id) {
		case PROP_SOURCE_LIST:
			g_value_set_object (value, priv->source_list);
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_source_combo_box_dispose (GObject *object)
{
	ESourceComboBoxPrivate *priv;

	priv = E_SOURCE_COMBO_BOX_GET_PRIVATE (object);

	if (priv->source_list != NULL) {
		g_object_unref (priv->source_list);
		priv->source_list = NULL;
	}

	g_hash_table_remove_all (priv->uid_index);

	/* Chain up to parent's "dispose" method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
e_source_combo_box_finalize (GObject *object)
{
	ESourceComboBoxPrivate *priv;

	priv = E_SOURCE_COMBO_BOX_GET_PRIVATE (object);

	g_hash_table_destroy (priv->uid_index);

	/* Chain up to parent's "finalize" method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
e_source_combo_box_class_init (ESourceComboBoxClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	parent_class = g_type_class_peek_parent (class);

	g_type_class_add_private (class, sizeof (ESourceComboBox));

	object_class->constructor = e_source_combo_box_constructor;
	object_class->set_property = e_source_combo_box_set_property;
	object_class->get_property = e_source_combo_box_get_property;
	object_class->dispose = e_source_combo_box_dispose;
	object_class->finalize = e_source_combo_box_finalize;

	g_object_class_install_property (
		object_class,
		PROP_SOURCE_LIST,
		g_param_spec_object (
			"source-list",
			"source-list",
			"List of sources to choose from",
			E_TYPE_SOURCE_LIST,
			G_PARAM_READWRITE));
}

static void
e_source_combo_box_init (ESourceComboBox *source_combo_box)
{
	source_combo_box->priv =
		E_SOURCE_COMBO_BOX_GET_PRIVATE (source_combo_box);

	source_combo_box->priv->uid_index =
		g_hash_table_new_full (
			g_str_hash, g_str_equal,
			(GDestroyNotify) g_free,
			(GDestroyNotify) gtk_tree_row_reference_free);
}

GType
e_source_combo_box_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo type_info = {
			sizeof (ESourceComboBoxClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) e_source_combo_box_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,  /* class_data */
			sizeof (ESourceComboBox),
			0,     /* n_preallocs */
			(GInstanceInitFunc) e_source_combo_box_init,
			NULL   /* value_table */
		};

		type = g_type_register_static (
			GTK_TYPE_COMBO_BOX, "ESourceComboBox", &type_info, 0);
	}

	return type;
}

/**
 * e_source_combo_box_new:
 * @source_list: an #ESourceList
 *
 * Creates a new #ESourceComboBox widget that lets the user pick an #ESource
 * from the provided #ESourceList.
 *
 * Returns: a new #ESourceComboBox
 **/
GtkWidget *
e_source_combo_box_new (ESourceList *source_list)
{
	g_return_val_if_fail (E_IS_SOURCE_LIST (source_list), NULL);

	return g_object_new (
		E_TYPE_SOURCE_COMBO_BOX,
		"source-list", source_list, NULL);
}

/**
 * e_source_combo_box_get_source_list:
 * @source_combo_box: an #ESourceComboBox
 *
 * Returns the #ESourceList which is acting as a data source for
 * @source_combo_box.
 *
 * Returns: an #ESourceList
 **/
ESourceList *
e_source_combo_box_get_source_list (ESourceComboBox *source_combo_box)
{
	ESourceList *source_list;

	g_return_val_if_fail (E_IS_SOURCE_COMBO_BOX (source_combo_box), NULL);

	g_object_get (source_combo_box, "source-list", &source_list, NULL);

	return source_list;
}

/**
 * e_source_combo_box_set_source_list:
 * @source_combo_box: an #ESourceComboBox
 * @source_list: an #ESourceList
 *
 * Sets the source list used by @source_combo_box to be @source_list.  This
 * causes the contents of @source_combo_box to be regenerated.
 **/
void
e_source_combo_box_set_source_list (ESourceComboBox *source_combo_box,
                                    ESourceList *source_list)
{
	g_return_if_fail (E_IS_SOURCE_COMBO_BOX (source_combo_box));
	g_return_if_fail (E_IS_SOURCE_LIST (source_list));

	g_object_set (source_combo_box, "source-list", source_list, NULL);
}

/**
 * e_source_combo_box_get_active:
 * @source_combo_box: an #ESourceComboBox
 *
 * Returns the #ESource corresponding to the currently active item, or %NULL
 * if there is no active item.
 *
 * Returns: an #ESource or %NULL
 **/
ESource *
e_source_combo_box_get_active (ESourceComboBox *source_combo_box)
{
	GtkComboBox *combo_box;
	GtkTreeIter iter;
	ESource *source;

	g_return_val_if_fail (E_IS_SOURCE_COMBO_BOX (source_combo_box), NULL);

	combo_box = GTK_COMBO_BOX (source_combo_box);

	if (!gtk_combo_box_get_active_iter (combo_box, &iter))
		return NULL;

	gtk_tree_model_get (
		gtk_combo_box_get_model (combo_box),
		&iter, COLUMN_SOURCE, &source, -1);

	return source;
}

/**
 * e_source_combo_box_set_active:
 * @source_combo_box: an #ESourceComboBox
 * @source: an #ESource
 *
 * Sets the active item to the one corresponding to @source.
 **/
void
e_source_combo_box_set_active (ESourceComboBox *source_combo_box,
                               ESource *source)
{
	g_return_if_fail (E_IS_SOURCE_COMBO_BOX (source_combo_box));
	g_return_if_fail (E_IS_SOURCE (source));

	e_source_combo_box_set_active_uid (
		source_combo_box, e_source_peek_uid (source));
}

/**
 * e_source_combo_box_get_active_uid:
 * @source_combo_box: an #ESourceComboBox
 *
 * Returns the unique ID of the #ESource corresponding to the currently
 * active item, or %NULL if there is no active item.
 *
 * Returns: a unique ID string or %NULL
 **/
const gchar *
e_source_combo_box_get_active_uid (ESourceComboBox *source_combo_box)
{
	ESource *source;

	g_return_val_if_fail (E_IS_SOURCE_COMBO_BOX (source_combo_box), NULL);

	source = e_source_combo_box_get_active (source_combo_box);
	if (source == NULL)
		return NULL;

	return e_source_peek_uid (source);
}

/**
 * e_source_combo_box_set_active_uid:
 * @source_combo_box: an #ESourceComboBox
 * @uid: a unique ID of an #ESource
 *
 * Sets the active item to the one corresponding to @uid.
 **/
void
e_source_combo_box_set_active_uid (ESourceComboBox *source_combo_box,
                                   const gchar *uid)
{
	ESourceComboBoxPrivate *priv;
	GtkTreeRowReference *reference;
	GtkComboBox *combo_box;
	GtkTreeIter iter;
	gboolean iter_was_set;

	g_return_if_fail (E_IS_SOURCE_COMBO_BOX (source_combo_box));
	g_return_if_fail (uid != NULL);

	priv = source_combo_box->priv;
	combo_box = GTK_COMBO_BOX (source_combo_box);

	reference = g_hash_table_lookup (priv->uid_index, uid);
	g_return_if_fail (gtk_tree_row_reference_valid (reference));

	iter_was_set = gtk_tree_model_get_iter (
		gtk_combo_box_get_model (combo_box), &iter,
		gtk_tree_row_reference_get_path (reference));
	g_return_if_fail (iter_was_set);

	gtk_combo_box_set_active_iter (combo_box, &iter);
}
