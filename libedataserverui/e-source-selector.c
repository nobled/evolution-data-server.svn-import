/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source-selector.c
 *
 * Copyright (C) 2003  Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#include <config.h>

#include "e-source-selector.h"

#include "e-util-marshal.h"

#include <gal/util/e-util.h>

#include <gtk/gtkmenu.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtktreestore.h>
#include <gtk/gtkcellrenderertoggle.h>
#include <gtk/gtkcellrenderertext.h>

#define PARENT_TYPE gtk_tree_view_get_type ()
static GtkTreeViewClass *parent_class = NULL;


struct _ESourceSelectorPrivate {
	ESourceList *list;

	GtkTreeStore *tree_store;

	GHashTable *selected_sources;

	int rebuild_model_idle_id;

	gboolean toggled_last;

	gboolean checkboxes_shown;
};

typedef struct {
	ESourceSelector *selector;

	GHashTable *remaining_uids;
	GSList *deleted_uids;

	gboolean selection_changed;
} ESourceSelectorRebuildData;

enum {
	SELECTION_CHANGED,
	PRIMARY_SELECTION_CHANGED,
	FILL_POPUP_MENU,
	NUM_SIGNALS
};
static unsigned int signals[NUM_SIGNALS] = { 0 };


/* Selection management.  */

static GHashTable *
create_selected_sources_hash (void)
{
	return g_hash_table_new_full (g_direct_hash, g_direct_equal,
				      (GDestroyNotify) g_object_unref, NULL);
}

static ESourceSelectorRebuildData *
create_rebuild_data (ESourceSelector *selector)
{
	ESourceSelectorRebuildData *rebuild_data;
	
	rebuild_data = g_new0 (ESourceSelectorRebuildData, 1);
	
	rebuild_data->selector = selector;
	rebuild_data->remaining_uids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, 
							      (GDestroyNotify) gtk_tree_row_reference_free);
	rebuild_data->deleted_uids = NULL;

	return rebuild_data;
}


static void
free_rebuild_data (ESourceSelectorRebuildData *rebuild_data)
{
	GSList *p;
	
	g_hash_table_destroy (rebuild_data->remaining_uids);
	for (p = rebuild_data->deleted_uids; p; p = p->next)
		gtk_tree_row_reference_free (p->data);
	g_slist_free (rebuild_data->deleted_uids);
	
	g_free (rebuild_data);
}

static gboolean
source_is_selected (ESourceSelector *selector,
		    ESource *source)
{
	if (g_hash_table_lookup (selector->priv->selected_sources, source) == NULL)
		return FALSE;
	else
		return TRUE;
}

static void
select_source (ESourceSelector *selector,
	       ESource *source)
{
	if (g_hash_table_lookup (selector->priv->selected_sources, source) != NULL)
		return;

	g_hash_table_insert (selector->priv->selected_sources, source, source);
	g_object_ref (source);
}

static void
unselect_source (ESourceSelector *selector,
		 ESource *source)
{
	if (g_hash_table_lookup (selector->priv->selected_sources, source) == NULL)
		return;

	/* (This will unref the source.)  */
	g_hash_table_remove (selector->priv->selected_sources, source);
}

static gboolean
find_source_iter (ESourceSelector *selector, ESource *source, GtkTreeIter *source_iter)
{
	GtkTreeModel *model = GTK_TREE_MODEL (selector->priv->tree_store);
	GtkTreeIter iter;

	if (gtk_tree_model_get_iter_first (model, &iter)) {
		do {
			if (gtk_tree_model_iter_children (model, source_iter, &iter)) {
				do {
					void *data;

					gtk_tree_model_get (model, source_iter, 0, &data, -1);
					g_assert (E_IS_SOURCE (data));

					if (E_SOURCE (data) == source)
						return TRUE;
				} while (gtk_tree_model_iter_next (model, source_iter));
			}
		} while (gtk_tree_model_iter_next (model, &iter));
	}

	return FALSE;
}

/* Setting up the model.  */
static gboolean
rebuild_existing_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	ESourceSelectorRebuildData *rebuild_data = data;
	void *node;
	const char *uid;

	gtk_tree_model_get (model, iter, 0, &node, -1);

	if (E_IS_SOURCE_GROUP (node)) {
		uid = e_source_group_peek_uid (E_SOURCE_GROUP (node));

		if (e_source_list_peek_group_by_uid (rebuild_data->selector->priv->list, uid)) {
			g_hash_table_insert (rebuild_data->remaining_uids, g_strdup (uid), 
					     gtk_tree_row_reference_new (model, path));
		} else {
			rebuild_data->deleted_uids = g_slist_prepend (rebuild_data->deleted_uids, 
								      gtk_tree_row_reference_new (model, path));
		}
		
		return FALSE;		
	}

	uid = e_source_peek_uid (E_SOURCE (node));
	if (e_source_list_peek_source_by_uid (rebuild_data->selector->priv->list, uid)) {
		g_hash_table_insert (rebuild_data->remaining_uids, g_strdup (uid), 
				     gtk_tree_row_reference_new (model, path));
	} else {
		rebuild_data->deleted_uids = g_slist_prepend (rebuild_data->deleted_uids, 
							      gtk_tree_row_reference_new (model, path));

		if (g_hash_table_remove (rebuild_data->selector->priv->selected_sources, node))
			rebuild_data->selection_changed = TRUE;
	}
	
	return FALSE;
}
	
static void
rebuild_model (ESourceSelector *selector)
{
	ESourceSelectorRebuildData *rebuild_data;
	GtkTreeStore *tree_store;
	GtkTreeIter iter;
	GSList *groups, *p;

	tree_store = selector->priv->tree_store;

	rebuild_data = create_rebuild_data (selector);

	/* Remove any delete sources or groups */
	gtk_tree_model_foreach (GTK_TREE_MODEL (tree_store), rebuild_existing_cb, rebuild_data);
	for (p = rebuild_data->deleted_uids; p; p = p->next) {
		GtkTreeRowReference *row_ref = p->data;
		GtkTreePath *path;
		
		path = gtk_tree_row_reference_get_path (row_ref);
		gtk_tree_model_get_iter (GTK_TREE_MODEL (tree_store), &iter, path);
		gtk_tree_store_remove (tree_store, &iter);

		gtk_tree_path_free (path);
	}
	
	/* Add new sources/groups or call row_changed in case they were renamed */
	groups = e_source_list_peek_groups (selector->priv->list);
	for (p = groups; p != NULL; p = p->next) {
		ESourceGroup *group = E_SOURCE_GROUP (p->data);
		GSList *sources, *q;
		GtkTreeRowReference *row_ref;
		
		row_ref = g_hash_table_lookup (rebuild_data->remaining_uids, e_source_group_peek_uid (group));
		if (!row_ref) {
			gtk_tree_store_append (GTK_TREE_STORE (tree_store), &iter, NULL);
			gtk_tree_store_set (GTK_TREE_STORE (tree_store), &iter, 0, group, -1);
		} else {
			GtkTreePath *path;
			
			path = gtk_tree_row_reference_get_path (row_ref);
			gtk_tree_model_get_iter (GTK_TREE_MODEL (tree_store), &iter, path);

			gtk_tree_model_row_changed (GTK_TREE_MODEL (tree_store), path, &iter);

			gtk_tree_path_free (path);
		}
		
		sources = e_source_group_peek_sources (group);
		for (q = sources; q != NULL; q = q->next) {
			ESource *source = E_SOURCE (q->data);
			GtkTreeIter child_iter;

			row_ref = g_hash_table_lookup (rebuild_data->remaining_uids, e_source_peek_uid (source));
			if (!row_ref) {
				gtk_tree_store_append (GTK_TREE_STORE (tree_store), &child_iter, &iter);
				gtk_tree_store_set (GTK_TREE_STORE (tree_store), &child_iter, 0, source, -1);

			} else {
				GtkTreePath *path;
				
				path = gtk_tree_row_reference_get_path (row_ref);
				gtk_tree_model_get_iter (GTK_TREE_MODEL (tree_store), &child_iter, path);
				
				gtk_tree_model_row_changed (GTK_TREE_MODEL (tree_store), path, &child_iter);

				gtk_tree_path_free (path);
			}
		}
	}

	if (rebuild_data->selection_changed)
		g_signal_emit (selector, signals[SELECTION_CHANGED], 0);

	if (!e_source_selector_peek_primary_selection (selector))
		e_source_selector_set_primary_selection	(selector, e_source_list_peek_source_any (selector->priv->list));
	
	free_rebuild_data (rebuild_data);
}

static int
on_idle_rebuild_model_callback (ESourceSelector *selector)
{
	rebuild_model (selector);
	selector->priv->rebuild_model_idle_id = 0;

	return FALSE;
}

static void
list_changed_callback (ESourceList *list,
		       ESourceSelector *selector)
{
	ESourceSelectorPrivate *priv = selector->priv;

	if (priv->rebuild_model_idle_id == 0)
		priv->rebuild_model_idle_id = g_idle_add ((GSourceFunc) on_idle_rebuild_model_callback,
							  selector);
}

static void
setup_model (ESourceSelector *selector)
{
	rebuild_model (selector);

	g_signal_connect_object (selector->priv->list, "changed", G_CALLBACK (list_changed_callback), G_OBJECT (selector), 0);
}


/* Data functions for rendering the model.  */

static void
toggle_cell_data_func (GtkTreeViewColumn *column,
		       GtkCellRenderer *renderer,
		       GtkTreeModel *model,
		       GtkTreeIter *iter,
		       ESourceSelector *selector)
{
	void *data;

	gtk_tree_model_get (model, iter, 0, &data, -1);

	if (E_IS_SOURCE_GROUP (data)) {
		g_object_set (renderer, "visible", FALSE, NULL);
	} else {
		g_assert (E_IS_SOURCE (data));

		g_object_set (renderer, "visible", selector->priv->checkboxes_shown, NULL);
		if (source_is_selected (selector, E_SOURCE (data)))
			g_object_set (renderer, "active", TRUE, NULL);
		else
			g_object_set (renderer, "active", FALSE, NULL);
	}
}

static void
text_cell_data_func (GtkTreeViewColumn *column,
		     GtkCellRenderer *renderer,
		     GtkTreeModel *model,
		     GtkTreeIter *iter,
		     ESourceSelector *selector)
{
	void *data;

	gtk_tree_model_get (model, iter, 0, &data, -1);

	if (E_IS_SOURCE_GROUP (data)) {
		g_object_set (renderer,
			      "text", e_source_group_peek_name (E_SOURCE_GROUP (data)),
			      "weight", PANGO_WEIGHT_BOLD,
			      "foreground_set", FALSE,
			      NULL);
	} else {
		ESource *source;
		guint32 color;
		gboolean has_color;

		g_assert (E_IS_SOURCE (data));
		source = E_SOURCE (data);

		g_object_set (renderer,
			      "text", e_source_peek_name (source),
			      "weight", PANGO_WEIGHT_NORMAL,
			      NULL);

		has_color = e_source_get_color (source, &color);
		if (!has_color) {
			g_object_set (renderer,
				      "foreground_set", FALSE,
				      NULL);
		} else {
			char *color_string = g_strdup_printf ("#%06x", color);
			g_object_set (renderer,
				      "foreground_set", TRUE,
				      "foreground", color_string,
				      NULL);
			g_free (color_string);
		}
	}
}

/* Custom selection function to make groups non selectable.  */
static gboolean
selection_func (GtkTreeSelection *selection,
		GtkTreeModel *model,
		GtkTreePath *path,
		gboolean path_currently_selected,
		ESourceSelector *selector)
{
	GtkTreeIter iter;
	void *data;

	if (selector->priv->toggled_last) {
		selector->priv->toggled_last = FALSE;
		
		return FALSE;
	}		

	if (path_currently_selected)
		return TRUE;

	if (! gtk_tree_model_get_iter (model, &iter, path))
		return FALSE;

	gtk_tree_model_get (model, &iter, 0, &data, -1);
	if (E_IS_SOURCE_GROUP (data))
		return FALSE;

	if (source_is_selected (selector, E_SOURCE (data)))
		return TRUE;

	e_source_selector_select_source (selector, E_SOURCE (data));

	return TRUE;
}


/* Callbacks.  */

static void
cell_toggled_callback (GtkCellRendererToggle *renderer,
		       const char *path_string,
		       ESourceSelector *selector)
{
	GtkTreeModel *model = GTK_TREE_MODEL (selector->priv->tree_store);
	GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
	GtkTreeIter iter;
	ESource *source;
	void *data;

	if (! gtk_tree_model_get_iter (model, &iter, path)) {
		gtk_tree_path_free (path);
		return;
	}

	gtk_tree_model_get (model, &iter, 0, &data, -1);
	if (! E_IS_SOURCE (data)) {
		gtk_tree_path_free (path);
		return;
	}

	source = E_SOURCE (data);
	if (source_is_selected (selector, source))
		unselect_source (selector, source);
	else
		select_source (selector, source);

	selector->priv->toggled_last = TRUE;
	
	gtk_tree_model_row_changed (model, path, &iter);
	g_signal_emit (selector, signals[SELECTION_CHANGED], 0);

	gtk_tree_path_free (path);
}

static void
selection_changed_callback (GtkTreeSelection *selection,
			    ESourceSelector *selector)
{
	g_signal_emit (selector, signals[PRIMARY_SELECTION_CHANGED], 0);
}

static gboolean
selector_button_press_event (GtkWidget *widget, GdkEventButton *event, ESourceSelector *selector)
{
	ESourceSelectorPrivate *priv = selector->priv;
	GtkWidget *menu;
	GtkTreePath *path;
	ESource *source = NULL;
	
	/* only process right-clicks */
	if (event->button != 3 || event->type != GDK_BUTTON_PRESS)
		return FALSE;

	/* Get the source/group */
	if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget), event->x, event->y, &path, NULL, NULL, NULL)) {
		GtkTreeIter iter;
		gpointer data;
		
		if (gtk_tree_model_get_iter (GTK_TREE_MODEL (priv->tree_store), &iter, path)) {
			gtk_tree_model_get (GTK_TREE_MODEL (priv->tree_store), &iter, 0, &data, -1);
			
			if (E_IS_SOURCE_GROUP (data))
				return FALSE;

			source = E_SOURCE (data);
		}
	}

	if (source)
		e_source_selector_set_primary_selection (selector, source);
	
	/* create the menu */
	menu = gtk_menu_new ();
	g_signal_emit (G_OBJECT (selector), signals[FILL_POPUP_MENU], 0, GTK_MENU (menu));

	/* popup the menu */
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, event->button, event->time);

	return TRUE;
}

/* GObject methods.  */

static void
impl_dispose (GObject *object)
{
	ESourceSelectorPrivate *priv = E_SOURCE_SELECTOR (object)->priv;

	if (priv->selected_sources != NULL) {
		g_hash_table_destroy (priv->selected_sources);
		priv->selected_sources = NULL;
	}

	if (priv->rebuild_model_idle_id != 0) {
		g_source_remove (priv->rebuild_model_idle_id);
		priv->rebuild_model_idle_id = 0;
	}

	if (priv->list != NULL) {
		g_object_unref (priv->list);
		priv->list = NULL;
	}

	if (priv->tree_store != NULL) {
		g_object_unref (priv->tree_store);
		priv->tree_store = NULL;
	}

	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
impl_finalize (GObject *object)
{
	ESourceSelectorPrivate *priv = E_SOURCE_SELECTOR (object)->priv;

	g_free (priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}


/* Initialization.  */

static void
class_init (ESourceSelectorClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	object_class->dispose  = impl_dispose;
	object_class->finalize = impl_finalize;

	parent_class = g_type_class_peek_parent (class);

	signals[SELECTION_CHANGED] = 
		g_signal_new ("selection_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceSelectorClass, selection_changed),
			      NULL, NULL,
			      e_util_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	signals[PRIMARY_SELECTION_CHANGED] = 
		g_signal_new ("primary_selection_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceSelectorClass, primary_selection_changed),
			      NULL, NULL,
			      e_util_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals[FILL_POPUP_MENU] =
		g_signal_new ("fill_popup_menu",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceSelectorClass, fill_popup_menu),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, G_TYPE_OBJECT);
}

static void
init (ESourceSelector *selector)
{
	ESourceSelectorPrivate *priv;
	GtkTreeViewColumn *column;
	GtkCellRenderer *cell_renderer;
	GtkTreeSelection *selection;

	priv = g_new0 (ESourceSelectorPrivate, 1);
	selector->priv = priv;

	g_signal_connect (G_OBJECT (selector), "button_press_event",
			  G_CALLBACK (selector_button_press_event), selector);

	priv->toggled_last = FALSE;
	priv->checkboxes_shown = TRUE;

	priv->selected_sources = create_selected_sources_hash ();

	priv->tree_store = gtk_tree_store_new (1, G_TYPE_POINTER);
	gtk_tree_view_set_model (GTK_TREE_VIEW (selector), GTK_TREE_MODEL (priv->tree_store));

	column = gtk_tree_view_column_new ();
	gtk_tree_view_append_column (GTK_TREE_VIEW (selector), column);

	cell_renderer = gtk_cell_renderer_toggle_new ();
	gtk_tree_view_column_pack_start (column, cell_renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func (column, cell_renderer, (GtkTreeCellDataFunc) toggle_cell_data_func, selector, NULL);
	g_signal_connect (cell_renderer, "toggled", G_CALLBACK (cell_toggled_callback), selector);

	cell_renderer = gtk_cell_renderer_text_new ();
	g_object_set (G_OBJECT (cell_renderer), "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE, NULL);
	gtk_tree_view_column_pack_start (column, cell_renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func (column, cell_renderer, (GtkTreeCellDataFunc) text_cell_data_func, selector, NULL);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (selector));
	gtk_tree_selection_set_select_function (selection, (GtkTreeSelectionFunc) selection_func, selector, NULL);
	g_signal_connect_object (selection, "changed", G_CALLBACK (selection_changed_callback), G_OBJECT (selector), 0);

	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (selector), FALSE);
}


/* Public API.  */

/**
 * e_source_selector_new:
 * @list: A source list.
 * 
 * Create a new view for @list.  The view will update automatically when @list
 * changes.
 * 
 * Return value: The newly created widget.
 **/
GtkWidget *
e_source_selector_new (ESourceList *list)
{
	ESourceSelector *selector;

	g_return_val_if_fail (E_IS_SOURCE_LIST (list), NULL);

	selector = g_object_new (e_source_selector_get_type (), NULL);

	selector->priv->list = list;
	g_object_ref (list);

	setup_model (selector);

	gtk_tree_view_expand_all (GTK_TREE_VIEW (selector));

	return GTK_WIDGET (selector);
}


/**
 * e_source_selector_get_selection:
 * @selector: 
 * 
 * Get the list of selected sources, i.e. those that were enabled through the
 * corresponding checkboxes in the tree.
 * 
 * Return value: A list of the ESources currently selected.  The sources will
 * be in the same order as they appear on the screen, and the list should be
 * freed using e_source_selector_free_selection().
 **/
GSList *
e_source_selector_get_selection (ESourceSelector *selector)
{
	GSList *selection_list;
	GSList *groups;
	GSList *p;

	g_return_val_if_fail (E_IS_SOURCE_SELECTOR (selector), NULL);

	selection_list = NULL;

	groups = e_source_list_peek_groups (selector->priv->list);
	for (p = groups; p != NULL; p = p->next) {
		ESourceGroup *group = E_SOURCE_GROUP (p->data);
		GSList *sources;
		GSList *q;

		sources = e_source_group_peek_sources (group);
		for (q = sources; q != NULL; q = q->next) {
			ESource *source = E_SOURCE (q->data);

			if (source_is_selected (selector, source)) {
				selection_list = g_slist_prepend (selection_list, source);
				g_object_ref (source);
			}
		}
	}

	return g_slist_reverse (selection_list);
}

/**
 * e_source_list_free_selection:
 * @list: A selection list returned by e_source_selector_get_selection().
 * 
 * Free the selection list.
 **/
void
e_source_selector_free_selection (GSList *list)
{
	g_slist_foreach (list, (GFunc) g_object_unref, NULL);
	g_slist_free (list);
}


/**
 * e_source_selector_show_selection:
 * @selector: An ESourceSelector widget
 * 
 * Specify whether the checkboxes in the ESourceSelector should be shown or
 * not.
 **/
void
e_source_selector_show_selection (ESourceSelector *selector,
				  gboolean show)
{
	g_return_if_fail (E_IS_SOURCE_SELECTOR (selector));

	show = !! show;
	if (show == selector->priv->checkboxes_shown)
		return;

	selector->priv->checkboxes_shown = show;

	gtk_tree_model_foreach (GTK_TREE_MODEL (selector->priv->tree_store),
				(GtkTreeModelForeachFunc) gtk_tree_model_row_changed,
				NULL);
}

/**
 * e_source_selector_selection_shown:
 * @selector: 
 * 
 * Check whether the checkboxes in the ESourceSelector are being shown or not.
 * 
 * Return value: %TRUE if the checkboxes are shown, %FALSE otherwise.
 **/
gboolean
e_source_selector_selection_shown (ESourceSelector *selector)
{
	g_return_val_if_fail (E_IS_SOURCE_SELECTOR (selector), FALSE);

	return selector->priv->checkboxes_shown;
}

/**
 * e_source_selector_select_source:
 * @selector: An ESourceSelector widget
 * @source: An ESource.
 * 
 * Select @source in @selector.
 **/
void
e_source_selector_select_source (ESourceSelector *selector,
				 ESource *source)
{
	GtkTreeIter source_iter;
	
	g_return_if_fail (E_IS_SOURCE_SELECTOR (selector));
	g_return_if_fail (E_IS_SOURCE (source));

	if (source_is_selected (selector, source))
		return;

	select_source (selector, source);

	if (find_source_iter (selector, source, &source_iter)) {
		GtkTreeModel *model = GTK_TREE_MODEL (selector->priv->tree_store);
		GtkTreePath *path;
		
		path = gtk_tree_model_get_path (model, &source_iter);
		gtk_tree_model_row_changed (model, path, &source_iter);
		gtk_tree_path_free (path);
		
		g_signal_emit (selector, signals[SELECTION_CHANGED], 0);
	}	
}

/**
 * e_source_selector_unselect_source:
 * @selector: An ESourceSelector widget
 * @source: An ESource.
 * 
 * Unselect @source in @selector.
 **/
void
e_source_selector_unselect_source (ESourceSelector *selector,
				   ESource *source)
{
	GtkTreeIter source_iter;

	g_return_if_fail (E_IS_SOURCE_SELECTOR (selector));
	g_return_if_fail (E_IS_SOURCE (source));

	if (! source_is_selected (selector, source))
		return;

	unselect_source (selector, source);

	if (find_source_iter (selector, source, &source_iter)) {
		GtkTreeModel *model = GTK_TREE_MODEL (selector->priv->tree_store);
		GtkTreePath *path;
		
		path = gtk_tree_model_get_path (model, &source_iter);
		gtk_tree_model_row_changed (model, path, &source_iter);
		gtk_tree_path_free (path);
		
		g_signal_emit (selector, signals[SELECTION_CHANGED], 0);
	}
}

/**
 * e_source_selector_source_is_selected:
 * @selector: An ESourceSelector widget
 * @source: An ESource.
 * 
 * Check whether @source is selected in @selector.
 * 
 * Return value: %TRUE if @source is currently selected, %FALSE otherwise.
 **/
gboolean
e_source_selector_source_is_selected (ESourceSelector *selector,
				      ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE_SELECTOR (selector), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	return source_is_selected (selector, source);
}

/**
 * e_source_selector_peek_primary_selection:
 * @selector: An ESourceSelector widget
 * 
 * Get the primary selected source.  The primary selection is the one that is
 * highlighted through the normal GtkTreeView selection mechanism (as opposed
 * to the "normal" selection, which is the set of source whose checkboxes are
 * checked).
 * 
 * Return value: The selected source.
 **/
ESource *
e_source_selector_peek_primary_selection (ESourceSelector *selector)
{
	GtkTreeModel *model = GTK_TREE_MODEL (selector->priv->tree_store);
	GtkTreeIter iter;
	void *data;

	if (! gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (selector)), NULL, &iter))
		return NULL;

	gtk_tree_model_get (model, &iter, 0, &data, -1);
	if (! E_IS_SOURCE (data))
		return NULL;

	return E_SOURCE (data);
}

/**
 * e_source_selector_set_primary_selection:
 * @selector: An ESourceSelector widget
 * @source: Source to select
 * 
 * Set the primary selected source.
 **/
void
e_source_selector_set_primary_selection (ESourceSelector *selector, ESource *source)
{
	GtkTreeIter source_iter;

	g_return_if_fail (selector != NULL);
	g_return_if_fail (E_IS_SOURCE_SELECTOR (selector));
	g_return_if_fail (source != NULL);
	g_return_if_fail (E_IS_SOURCE (source));

	if (find_source_iter (selector, source, &source_iter)) {
		GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (selector));
		gtk_tree_selection_select_iter (selection, &source_iter);
	} else {
		g_warning (G_STRLOC ": Cannot find source %p (%s) in selector %p", 
			   source, e_source_peek_name (source), selector);
	}
}


E_MAKE_TYPE (e_source_selector, "ESourceSelector", ESourceSelector, class_init, init, PARENT_TYPE)
