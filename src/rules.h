/**
 * molt - Copyright (C) 2012 Olivier Brunel
 *
 * rules.h
 * Copyright (C) 2012 Olivier Brunel <i.am.jack.mail@gmail.com>
 * 
 * This file is part of molt.
 *
 * molt is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * molt is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * molt. If not, see http://www.gnu.org/licenses/
 */

#ifndef RULES_CASE_H
#define	RULES_CASE_H

#ifdef	__cplusplus
extern "C" {
#endif

/* glib */
#include <glib-2.0/glib.h>

/* molt */
#include "molt.h"

#define MOLT_RULE_ERROR		g_quark_from_static_string ("molt rule error")

gboolean
rule_to_lower (gpointer    *data,
               const gchar *name,
               gchar      **new_name,
               GError     **error);

gboolean
rule_to_upper (gpointer    *data,
               const gchar *name,
               gchar      **new_name,
               GError     **error);

gboolean
rule_camel (gpointer    *data,
            const gchar *name,
            gchar      **new_name,
            GError     **error);

gboolean
rule_sr_init (gpointer  *data,
              GPtrArray *params,
              GError   **error);
void
rule_sr_destroy (gpointer *data);
gboolean
rule_sr (gpointer    *_data,
         gchar       *name,
		 gchar      **new_name,
		 GError     **error);


gboolean
rule_list_init (gpointer  *data,
                GPtrArray *params,
                GError   **error);
gboolean
rule_list (gpointer    *data,
           const gchar *name,
           gchar      **new_name,
           GError     **error);


gboolean
rule_regex_init (gpointer  *_data,
                 GPtrArray *params,
                 GError   **error);
void
rule_regex_destroy (gpointer *data);
gboolean
rule_regex (gpointer    *data,
            const gchar *name,
            gchar      **new_name,
            GError     **error);

gboolean
rule_variables (gpointer    *data,
                const gchar *name,
                gchar      **new_name,
                GError     **error);

gboolean
rule_tpl_init (gpointer  *data,
               GPtrArray *params,
               GError   **error);
gboolean
rule_tpl (gpointer    *data,
          const gchar *name,
          gchar      **new_name,
          GError     **error);


#ifdef	__cplusplus
}
#endif

#endif	/* RULES_CASE_H */

