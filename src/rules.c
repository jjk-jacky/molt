/**
 * molt - Copyright (C) 2012 Olivier Brunel
 *
 * rules.c
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

#define _GNU_SOURCE     /* for strcasestr() in string.h */

#define _UNUSED_            __attribute__ ((unused)) 

/* C */
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* molt */
#include "rules.h"
#include "internal.h"

gboolean
rule_to_lower (gpointer    *data _UNUSED_,
               const gchar *name,
               gchar      **new_name,
               GError     **error _UNUSED_)
{
	*new_name = g_utf8_strdown (name, -1);
	return TRUE;
}

gboolean
rule_to_upper (gpointer    *data _UNUSED_,
               const gchar *name,
               gchar      **new_name,
               GError     **error _UNUSED_)
{
    *new_name = g_utf8_strup (name, -1);
    return TRUE;
}

gboolean
rule_camel (gpointer    *data _UNUSED_,
            const gchar *name,
            gchar      **new_name,
            GError     **error _UNUSED_)
{
    gchar *s;
    gchar *e;
    gboolean do_next= TRUE;
    
    /* to lower */
    *new_name = g_utf8_strdown (name, -1);
    /* find the last dot, considered the extension */
    e = strrchr (*new_name, '.');
    /* we'll turn to upper each char after a space/punct, but not touch the ext */
    for (s = *new_name; *s != '\0' && (!e || s < e); ++s)
    {
        if (ispunct (*s))
        {
            do_next = TRUE;
        }
        else if (do_next)
        {
            *s = (gchar) toupper (*s);
            do_next = FALSE;
        }
    }
    
    return TRUE;
}

typedef gchar *(*strstr_fn) (const gchar *haystack, const gchar *needle);
typedef struct {
    const gchar *search;
    size_t       len_search;
    const gchar *replace;
    size_t       len_replace;
    strstr_fn    strstr;
} sr_t;

gboolean
rule_sr_init (gpointer  *data,
              GPtrArray *params,
              GError   **error)
{
    sr_t *d;
    gchar *options;
    
    /* make sure we have something to search for */
    if (!params || params->len < 1)
    {
        g_set_error (error, MOLT_RULE_ERROR, 1,
                     "Parameter(s) missing");
        return FALSE;
    }
    /* and not too many params */
    else if (params->len > 3)
    {
        g_set_error (error, MOLT_RULE_ERROR, 1,
                     "Too many parameters; syntax: search[/replace[/options]]");
        return FALSE;
    }
    
    *data = g_malloc0 (sizeof (*d));
    d = *data;
    
    d->search = g_ptr_array_index (params, 0);
    d->len_search = strlen (d->search);
    if (params->len >= 2)
    {
        d->replace = g_ptr_array_index (params, 1);
        d->len_replace = strlen (d->replace);
    }
    
    if (params->len == 3)
    {
        options = g_ptr_array_index (params, 2);
    }
    if (!options || strcmp (options, "i") != 0)
    {
        d->strstr = strstr;
    }
    else
    {
        d->strstr = strcasestr;
    }
    
    return TRUE;
}

void
rule_sr_destroy (gpointer *data)
{
    g_free (*data);
}

gboolean
rule_sr (gpointer    *_data,
         gchar       *name,
		 gchar      **new_name,
		 GError     **error _UNUSED_)
{
    sr_t        *data = *_data;
    size_t       len_org;
    size_t       nb;
    gchar       *s;
    gchar       *ss;
    GPtrArray   *sub;
    size_t       i;
    
    len_org = strlen (name);
    /* this will hold pointers to the different parts in name */
    sub = g_ptr_array_new ();
    nb = 0;
    s = name;
    while ((s = data->strstr (s, data->search)))
    {
        /* found it */
        ++nb;
        /* make the string ends here, so we can easily add that part */
        *s = '\0';
        /* move right after the searched for part */
        s += data->len_search;
        /* and store that location */
        g_ptr_array_add (sub, (gpointer) s);
    }
    
    /* did we find anything? */
    if (nb == 0)
    {
        *new_name = NULL;
        g_ptr_array_free (sub, TRUE);
        return TRUE;
    }
    
    /* alloc memory for the new name:
     * - size of original, minus what we're replacing (searched for)
     * - adding size of replacement(s)
     * - and 1 for NULL */
    *new_name = g_malloc0 (
        (len_org - (nb * data->len_search) + (nb * data->len_replace) + 1)
        * sizeof (**new_name));
    
    /* put what was before the first match */
    s = g_stpcpy (*new_name, name);
    /* then add everything */
    for (i = 0; i < nb; ++i)
    {
        /* if we have a replacement, add it */
        if (data->replace)
        {
            s = g_stpcpy (s, data->replace);
        }
        /* get pointer to the part that came after */
        ss = g_ptr_array_index (sub, i);
        /* and add it */
        s = g_stpcpy (s, ss);
        /* now move back & restore things */
        ss -= data->len_search;
        *ss = data->search[0];
    }
    g_ptr_array_free (sub, TRUE);
    
    return TRUE;
}

gboolean
rule_list_init (gpointer  *data,
                GPtrArray *params _UNUSED_,
                GError   **error)
{
    GError *local_err = NULL;
    
    if (G_UNLIKELY (!get_stdin (data, &local_err)))
    {
        g_set_error (error, MOLT_RULE_ERROR, 1, "Unable to get stdin: %s",
                     local_err->message);
        g_clear_error (&local_err);
        return FALSE;
    }
    
    return TRUE;
}

gboolean
rule_list (gpointer    *data,
           const gchar *name _UNUSED_,
           gchar      **new_name,
           GError     **error _UNUSED_)
{
    FILE *stream = *data;
    char  buf[PATH_MAX];
    char *s;
    
    if (!fgets ((char *)&buf, PATH_MAX, stream))
    {
        /* success w/out a name, so if there are more files than names on the
         * list given on stdin, we just don't rename the last files (at least,
         * not by this rule anyways) */
        *new_name = NULL;
        return TRUE;
    }
    
    /* because when reading from stdin, there's a newline at the end that we
     * don't want in the file name
     * (yeah, if someone wanted to use a LF in their file name that wouldn't
     * be possible using this rule. also, it would probably be a bad idea...) */
    for (s = buf; *s != '\0'; ++s)
    {
        if (*s == '\n')
        {
            *s = '\0';
            break;
        }
    }
    
    *new_name = strdup (buf);
    return TRUE;
}

typedef struct {
    GRegex      *regex;
    const gchar *replacement;
} regex_t;

gboolean
rule_regex_init (gpointer  *data,
                 GPtrArray *params,
                 GError   **error)
{
    GError              *local_err = NULL;
    const gchar         *pattern;
    const gchar         *replacement;
    const gchar         *options;
    GRegexCompileFlags   flags;
    GRegex              *regex;
    regex_t             *d;
    
    /* make sure we have a pattern */
    if (!params || params->len < 1)
    {
        g_set_error (error, MOLT_RULE_ERROR, 1,
                     "Pattern missing");
        return FALSE;
    }
    else if (params->len > 3)
    {
        g_set_error (error, MOLT_RULE_ERROR, 1,
                     "Too many parameters");
        return FALSE;
    }
    
    pattern = g_ptr_array_index (params, 0);
    if (params->len >= 2)
    {
        replacement = g_ptr_array_index (params, 1);
    }
    else
    {
        replacement = "";
    }
    
    flags = G_REGEX_OPTIMIZE;
    if (params->len == 3)
    {
        options = g_ptr_array_index (params, 2);
        if (options && strcmp (options, "i") == 0)
        {
            flags |= G_REGEX_CASELESS;
        }
    }
    
    regex = g_regex_new (pattern, flags, 0, &local_err);
    if (local_err)
    {
        g_set_error (error, MOLT_RULE_ERROR, 1,
                     "Unable to compile regex: %s", local_err->message);
        g_clear_error (&local_err);
        return FALSE;
    }
    
    if (!g_regex_check_replacement (replacement, NULL, &local_err))
    {
        g_set_error (error, MOLT_RULE_ERROR, 1,
                     "Invalid replacement: %s", local_err->message);
        g_clear_error (&local_err);
        g_regex_unref (regex);
        return FALSE;
    }
    
    *data = g_malloc0 (sizeof (*d));
    d = *data;
    d->regex = regex;
    d->replacement = replacement;
    
    return TRUE;
}

void
rule_regex_destroy (gpointer *data)
{
    g_regex_unref (((regex_t *)*data)->regex);
    g_free (*data);
}

gboolean
rule_regex (gpointer    *data,
            const gchar *name,
            gchar      **new_name,
            GError     **error)
{
    GError *local_err = NULL;
    regex_t *regex = *data;
    
    *new_name = g_regex_replace (regex->regex, name, -1, 0, regex->replacement,
                                 0, &local_err);
    if (local_err)
    {
        g_set_error (error, MOLT_RULE_ERROR, 1,
                     "Failed to process regex: %s", local_err->message);
        g_clear_error (&local_err);
        return FALSE;
    }
    
    return TRUE;
}


gboolean
rule_variables (gpointer    *data _UNUSED_,
                const gchar *name _UNUSED_,
                gchar      **new_name _UNUSED_,
                GError     **error _UNUSED_)
{
    /* this rule doesn't actually do anything, it's just a rule with the flag
     * resolve_variables enabled, to do just that. But that is done by molt, and
     * the rule itself doesn't do anything */
    return TRUE;
}


gboolean
rule_tpl_init (gpointer  *data,
               GPtrArray *params,
               GError   **error)
{
    if (!params || params->len != 1)
    {
         g_set_error (error, MOLT_RULE_ERROR, 1,
                      "Template missing");
        return FALSE;
    }
    
    *data = g_ptr_array_index (params, 0);
    return TRUE;
}

gboolean
rule_tpl (gpointer    *data,
          const gchar *name _UNUSED_,
          gchar      **new_name,
          GError     **error _UNUSED_)
{
    *new_name = g_strdup (*data);
    return TRUE;
}
