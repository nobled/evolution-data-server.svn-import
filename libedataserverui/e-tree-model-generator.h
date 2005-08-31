/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-tree-model-generator.h - Model wrapper that permutes underlying rows.
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: Hans Petter Jansson <hpj@novell.com>
 */

#ifndef E_TREE_MODEL_GENERATOR_H
#define E_TREE_MODEL_GENERATOR_H

#include <gtk/gtktreemodel.h>

G_BEGIN_DECLS

#define E_TYPE_TREE_MODEL_GENERATOR            (e_tree_model_generator_get_type ())
#define E_TREE_MODEL_GENERATOR(obj)	       (GTK_CHECK_CAST ((obj), E_TYPE_TREE_MODEL_GENERATOR, ETreeModelGenerator))
#define E_TREE_MODEL_GENERATOR_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), E_TYPE_TREE_MODEL_GENERATOR, ETreeModelGeneratorClass))
#define E_IS_TREE_MODEL_GENERATOR(obj)         (GTK_CHECK_TYPE ((obj), E_TYPE_TREE_MODEL_GENERATOR))
#define E_IS_TREE_MODEL_GENERATOR_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), E_TYPE_TREE_MODEL_GENERATOR))
#define E_TREE_MODEL_GENERATOR_GET_CLASS(obj)  (GTK_CHECK_GET_CLASS ((obj), E_TYPE_TREE_MODEL_GENERATOR, ETreeModelGeneratorClass))

typedef gint (*ETreeModelGeneratorGenerateFunc) (GtkTreeModel *model, GtkTreeIter *child_iter,
						 gpointer data);
typedef void (*ETreeModelGeneratorModifyFunc)   (GtkTreeModel *model, GtkTreeIter *child_iter,
						 gint permutation_n, gint column, GValue *value,
						 gpointer data);

typedef struct ETreeModelGenerator       ETreeModelGenerator;
typedef struct ETreeModelGeneratorClass  ETreeModelGeneratorClass;

struct ETreeModelGeneratorClass {
	GObjectClass parent_class;
};

struct ETreeModelGenerator {
	GObject                          parent;

	/* Private */

	gint                             stamp;
	GtkTreeModel                    *child_model;
	GArray                          *root_nodes;

	ETreeModelGeneratorGenerateFunc  generate_func;
	gpointer                         generate_func_data;

	ETreeModelGeneratorModifyFunc    modify_func;
	gpointer                         modify_func_data;
};

GtkType              e_tree_model_generator_get_type                   (void);

ETreeModelGenerator *e_tree_model_generator_new                        (GtkTreeModel *child_model);
GtkTreeModel        *e_tree_model_generator_get_model                  (ETreeModelGenerator *tree_model_generator);

void                 e_tree_model_generator_set_generate_func          (ETreeModelGenerator *tree_model_generator,
									ETreeModelGeneratorGenerateFunc func,
									gpointer data, GtkDestroyNotify destroy);
void                 e_tree_model_generator_set_modify_func            (ETreeModelGenerator *tree_model_generator,
									ETreeModelGeneratorModifyFunc func,
									gpointer data, GtkDestroyNotify destroy);

GtkTreePath         *e_tree_model_generator_convert_child_path_to_path (ETreeModelGenerator *tree_model_generator,
									GtkTreePath *child_path);
void                 e_tree_model_generator_convert_child_iter_to_iter (ETreeModelGenerator *tree_model_generator,
									GtkTreeIter *generator_iter,
									GtkTreeIter *child_iter);
GtkTreePath         *e_tree_model_generator_convert_path_to_child_path (ETreeModelGenerator *tree_model_generator,
									GtkTreePath *generator_path);
void                 e_tree_model_generator_convert_iter_to_child_iter (ETreeModelGenerator *tree_model_generator,
									GtkTreeIter *child_iter,
									gint *permutation_n,
									GtkTreeIter *generator_iter);

G_END_DECLS

#endif  /* E_TREE_MODEL_GENERATOR_H */
