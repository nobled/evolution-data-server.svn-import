/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2001 Ximain, Inc. (www.ximian.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */


#ifndef CAMEL_SMIME_UTILS_H
#define CAMEL_SMIME_UTILS_H

#include <glib.h>
#include <camel/camel-mime-part.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

gboolean camel_smime_is_smime_v3_signed (CamelMimePart *part);

gboolean camel_smime_is_smime_v3_encrypted (CamelMimePart *part);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ! CAMEL_SMIME_UTILS_H */
