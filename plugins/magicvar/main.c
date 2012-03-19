/**
 * molt - Copyright (C) 2012 Olivier Brunel
 *
 * plugins/magicvar/main.c
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

/* C */
#include <stdio.h>
#include <string.h>

/* molt */
#include "molt.h"

/* interface with molt (set by molt upon loading) */
plugin_t *molt_plugin;

/* list of commands to run (key are variable names, values cmdlines) */
GHashTable *variables = NULL;

#define CONF_FILE       "%s/.config/molt/magicvar.conf"
#define MAGICVAR_ERROR  g_quark_from_static_string ("magicvar error")

PLUGIN_VERSION_CHECK (MOLT_API_VERSION)

PLUGIN_SET_INFO ("Magic Variable",
    "Provides a \"magic\" variable _ that obtains its value through execution of\n"
    "an external process.",
    "0.0.1",
    "Olivier Brunel")

void
plugin_init (void)
{
    
}

static gchar *
get_value (const gchar *file, GPtrArray *params, GError **error)
{
    GError      *local_err = NULL;
    const gchar *var;
    const gchar *value;
    gchar       *cmdline;
    gchar       *s, *d;
    gchar       *out;
    gchar       *err;
    gint         exit;
    size_t       len;
    size_t       len_file;
    gint         i;
    
    if (!params)
    {
        g_set_error (error, MAGICVAR_ERROR, 1, "Parameters missing");
        return NULL;
    }
    else if (params->len < 1)
    {
        g_set_error (error, MAGICVAR_ERROR, 1, "Variable name required");
        return NULL;
    }
    
    /* first param is the variable name */
    var = g_ptr_array_index (params, 0);
    if (!variables)
    {
        g_set_error (error, MAGICVAR_ERROR, 1, "Unknown variable: %s", var);
        return NULL;
    }
    /* look for a cmdline for this variable */
    value = g_hash_table_lookup (variables, var);
    if (!value)
    {
        g_set_error (error, MAGICVAR_ERROR, 1, "Unknown variable: %s", var);
        return NULL;
    }
    molt_debug (LEVEL_VERBOSE, "cmdline found: %s\n", value);
    
    /* calculate length of cmdline */
    len_file = strlen (file);
    len = 1 + strlen (value) + len_file;
    for (i = 1; i < params->len; ++i)
    {
        len += strlen (params->pdata[i]) + 1; /* +1 to space-separate params */
    }
    
    /* allocate memory */
    cmdline = g_malloc (len * sizeof (*cmdline));
    /* copy cmdline (value) replacing placeholders */
    for (s = (gchar *) value, d = cmdline; *s; ++s, ++d)
    {
        /* a placeholder? */
        if (*s == '%')
        {
            /* %F : file */
            if (*(s + 1) == 'F')
            {
                /* copy filename */
                memcpy (d, file, len_file);
                /* move dest by len_file - 1, since we'll move by 1 automatically */
                d += len_file - 1;
            }
            /* %P : params */
            else if (*(s + 1) == 'P')
            {
                /* copy params */
                for (i = 1; i < params->len; ++i)
                {
                    len = strlen (params->pdata[i]);
                    memcpy (d, params->pdata[i], len);
                    /* move past it */
                    d += len;
                    /* separator */
                    *d++ = ' ';
                }
                /* after the last param, we need to back off 1 since there's
                 * no separator then */
                --d;
            }
            /* move source by one more */
            ++s;
        }
        else
        {
            /* basic copy */
            *d = *s;
        }
    }
    *d = '\0';
    molt_debug (LEVEL_VERBOSE, "cmdline parsed: %s\n", cmdline);
    
    if (!g_spawn_command_line_sync (cmdline, &out, &err, &exit, &local_err))
    {
        g_free (cmdline);
        g_set_error (error, MAGICVAR_ERROR, 1, "Error running %s: %s",
                     cmdline, local_err->message);
        g_clear_error (&local_err);
        return NULL;
    }
    else if (exit != 0)
    {
        molt_debug (LEVEL_VERBOSE, "output: %s\n", out);
        molt_debug (LEVEL_VERBOSE, "error: %s\n", err);
        g_set_error (error, MAGICVAR_ERROR, 1, "Command failed: %s: %s",
                     cmdline, err);
        g_clear_error (&local_err);
        g_free (cmdline);
        g_free (err);
        g_free (out);
        return NULL;
    }
    
    g_free (cmdline);
    g_free (err);
    
    /* often times the output will end with a LF that we don't want in the filename */
    len = strlen (out);
    if (len > 0 && out[len - 1] == '\n')
    {
        out[len - 1] = '\0';
    }
    
    return out;
}

void
plugin_init_vars (void)
{
    GError   *local_err = NULL;
    var_def_t variable;
    GKeyFile *keyfile;
    gchar     file[4096];
    gchar   **keys, **k;
    gchar    *key;
    gchar    *value;
    
    variable.name = "_";
    variable.description = "Magic variable: specify \"variable\" as parameter";
    variable.help = "The first parameter is the name of the \"variable\" to resolve.\n"
        "Resolving is done running the corresponding command line, using output\n"
        "as value (removing trailing newline (\\n) if present)";
    variable.type = VAR_TYPE_PER_FILE;
    variable.param = PARAM_SPLIT;
    variable.get_value = get_value;
    molt_add_var (&variable);
    
    snprintf (file, 4096, CONF_FILE, g_get_home_dir ());
    keyfile = g_key_file_new ();
    if (!g_key_file_load_from_file (keyfile, file, G_KEY_FILE_NONE, &local_err))
    {
        molt_debug (LEVEL_DEBUG, "unable to read config from %s: %s\n",
                    file, local_err->message);
        g_clear_error (&local_err);
        return;
    }
    
    keys = g_key_file_get_keys (keyfile, "variables", NULL, &local_err);
    if (local_err)
    {
        molt_debug (LEVEL_DEBUG, "failed to get keys from [variables] in %s: %s\n",
                    file, local_err->message);
        g_clear_error (&local_err);
        g_strfreev (keys);
        g_key_file_free (keyfile);
        return;
    }
    
    variables = g_hash_table_new_full (g_str_hash, g_str_equal,
                                       (GDestroyNotify) g_free,
                                       (GDestroyNotify) g_free);
    
    for (k = keys, key = *k; key; key = *++k)
    {
        value = g_key_file_get_value (keyfile, "variables", key, &local_err);
        if (G_LIKELY (!local_err))
        {
            g_hash_table_insert (variables, key, value);
        }
        else
        {
            molt_debug (LEVEL_DEBUG, "failed to get value of %s in %s: %s\n",
                        key, file, local_err->message);
            g_clear_error (&local_err);
        }
    }
    
    g_free (keys);
    g_key_file_free (keyfile);
}

void
plugin_destroy (void)
{
    if (variables)
    {
        g_hash_table_destroy (variables);
    }
}
