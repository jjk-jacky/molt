
#define IS_MOLT

/* C */
#include <stdio.h>
#include <stdlib.h> /* exit */
#include <string.h>
#include <time.h> /* for debug() */
#include <unistd.h> /* getcwd */
#include <errno.h>

/* glib */
#include <gmodule.h>

/* molt */
#include "molt.h"
#include "internal.h"
#include "main.h"
/* rules */
#include "rules.h"
/* variables */
#include "variables.h"

/* verbose/debug level */
static level_t     level            = 0;
/* return/error code */
static error_t     err              = ERROR_NONE;
/* list of error messages */
static GSList     *errors           = NULL;
/* list of plugins */
static GSList     *plugins          = NULL;
/* list of rules (rule_def_t) */
static GHashTable *rules            = NULL;
/* list of actions to process (not static for use in actions.c ) */
GHashTable        *actions          = NULL;
/* list of taken new names: that is, when an action wants to take a (new) name
 * it will be added to this list (whether it is to-rename or conflict-FS, as the
 * later could be resolved later on) unless there's already one, in which case
 * it'll be set to conflict.
 * We don't keep track of all actions wanting to take that new name, but only
 * whether or not there's (already) one, to detect conflicts. 
 * (not static for use in actions.c ) */
GHashTable        *new_names        = NULL;
/* number of conflicts (standard & FS) (not static for use in actions.c ) */
gint               nb_conflicts     = 0;
/* number of actions requiring two-steps renaming jobs (not static for use in actions.c ) */
gint               nb_two_steps     = 0;
/* current pathname */
static gchar      *curdir           = NULL;
/* whether rules are given the full path/filename (or just filename) */
static gboolean    process_fullname = FALSE;
/* whether output shows the full path/filename (or just filename) */
static gboolean    output_fullname  = FALSE;
/* whether rules can give a new name with slashes or not */
/* Note: only if process_fullname == FALSE obviously */
static gboolean    allow_path       = FALSE;
/* list of supported variables */
static GHashTable *variables        = NULL;
/* cached values for per-file variables */
static GHashTable *var_per_file     = NULL;
/* cached values for global variables */
static GHashTable *var_global       = NULL;

void
debug (level_t lvl, const gchar *fmt, ...)
{
    va_list    args;
    time_t     now;
    struct tm *ptr;
    gchar      buf[10];
    
    if (lvl > level)
    {
        return;
    }
    
    now = time (NULL);
    ptr = localtime (&now);
    strftime (buf, 10, "%H:%M:%S", ptr);
    fprintf (stdout, "[%s] ", buf);
    
    va_start (args, fmt);
    vfprintf (stdout, fmt, args);
    va_end (args);
}

static void
error (error_t code, const gchar *fmt, ...)
{
    va_list  args;
    gchar   *msg;
    
    /* update return/error code */
    err |= code;
    
    /* print the new error message */
    va_start (args, fmt);
    msg = g_strdup_vprintf (fmt, args);
    va_end (args);
    
    /* add it to the list of errors */
    debug (LEVEL_DEBUG, "register error: [%d] %s", code, msg);
    errors = g_slist_append (errors, msg);
}

static void
error_out (gboolean do_exit)
{
    GSList *l;
    
    for (l = errors; l; l = l->next)
    {
        fprintf (stderr, "%s", (gchar *) l->data);
        g_free (l->data);
    }
    g_slist_free (errors);
    errors = NULL;
    
    if (do_exit)
    {
        if (G_UNLIKELY (err == 0))
        {
            err = 255;
        }

        free_memory();
        exit (err);
    }
}

gboolean
get_stdin (gpointer *stream, GError **_error)
{
    static gboolean is_taken = FALSE;
    
    if (G_UNLIKELY (is_taken))
    {
        g_set_error (_error, MOLT_ERROR, 1, "stdin can only be taken once");
        return FALSE;
    }
    
    is_taken = TRUE;
    *stream = stdin;
    return TRUE;
}

gboolean
add_rule (rule_def_t *rule)
{
    rule_def_t *new_rule;
    
    /* make sure there isn't already a rule with that name */
    if (G_UNLIKELY (g_hash_table_lookup (rules, rule->name)))
    {
        debug (LEVEL_DEBUG, "cannot add rule %s: already one\n", rule->name);
        return FALSE;
    }
    
    /* create our own copy of the rule_def_t */
    new_rule = g_slice_new (rule_def_t);
    memcpy (new_rule, rule, sizeof (rule_def_t));
    /* and store it in our hashmap of rules */
    g_hash_table_insert (rules, (gpointer) new_rule->name, (gpointer) new_rule);
    
    debug (LEVEL_DEBUG, "added rule %s\n", new_rule->name);
    return TRUE;
}

static void
free_rule (rule_def_t *rule)
{
    debug (LEVEL_VERBOSE, "free-ing rule %s\n", rule->name);
    g_slice_free (rule_def_t, rule);
}

gboolean
add_var (var_def_t *variable)
{
    var_def_t *new_variable;
    
    /* make sure there isn't already a variable with that name */
    if (G_UNLIKELY (g_hash_table_lookup (variables, variable->name)))
    {
        debug (LEVEL_DEBUG, "cannot add variable %s: already one\n", variable->name);
        return FALSE;
    }
    
    /* create our own copy of the var_def_t */
    new_variable = g_slice_new (var_def_t);
    memcpy (new_variable, variable, sizeof (var_def_t));
    /* and store it in our hashmap of variables */
    g_hash_table_insert (variables,
                         (gpointer) new_variable->name,
                         (gpointer) new_variable);
    
    debug (LEVEL_DEBUG, "added variable %s\n", new_variable->name);
    return TRUE;
}

static void
free_variable (var_def_t *variable)
{
    debug (LEVEL_VERBOSE, "free-ing variable %s\n", variable->name);
    g_slice_free (var_def_t, variable);
}

static void
free_action (action_t *action)
{
    debug (LEVEL_VERBOSE, "free-ing action for %s\n", action->file);
    g_free (action->file);
    if (action->tmp_name)
    {
        g_free (action->tmp_name);
    }
    if (action->new_name)
    {
        g_free (action->new_name);
    }
    if (action->error)
    {
        g_free (action->error);
    }
    g_slice_free (action_t, action);
}

static gchar *
strpchr (gchar *string, gchar *start, gchar c)
{
    for ( ; *start != c && start > string; --start)
        ;
    return (*start == c) ? start : NULL;
}

static void
set_full_file_name (gchar *file, gchar **fullname, gchar **filename)
{
    gchar *path;
    gchar *s, *p;
    
    debug (LEVEL_VERBOSE, "setting {full,file}name for %s\n", file);
    
    if (file[0] == '/')
    {
        *fullname = g_malloc (strlen (file) + 1);
        sprintf (*fullname, "%s", file);
        
    }
    else
    {
        *fullname = g_malloc (strlen (curdir) + strlen (file) + 2);
        sprintf (*fullname, "%s/%s", curdir, file);
    }
    
    path = *fullname + 1;
    while ((s = strchr (path, '/')))
    {
        if (s == path + 1 && *path == '.')
        {
            /* simply move the part after "./" to remove it */
            memmove (path, s + 1, strlen (s + 1) + 1);
            /* no need to move path, since we move the string within */
        }
        else if (s == path + 2 && *path == '.' && *(path + 1) == '.')
        {
            /* find the "/" before the current one */
            p = strpchr (*fullname, path - 2, '/');
            if (!p)
            {
                break;
            }
            /* and move the part after "../" back before */            
            memmove (p + 1, s + 1, strlen (s + 1) + 1);
            /* now update path to point to the newly copied path */
            path = p + 1;
        }
        else
        {
            path = s + 1;
        }
    }
    
    s = strrchr (*fullname, '/');
    *filename = s + 1;
    debug (LEVEL_DEBUG, "fullname=%s -- filename=%s\n", *fullname, *filename);
}

static void
split_params (gchar c, gchar *arg, GPtrArray **params)
{
    gchar *a, *p, *s, *ss;
    gint   i;
    size_t len;
    
    debug (LEVEL_DEBUG, "splitting params: %s\n", arg);
    
    *params = g_ptr_array_new ();
    a = p = arg;
    while ((s = strchr (a, c)))
    {
        /* make sure it's not escaped */
        for (i = 1, ss = s - 1; ss >= a && *ss == '\\'; --ss, ++i)
            ;
        if (!(i % 2))
        {
            /* it's escaped, so we need to move everything to remove the \ */
            len = strlen (s - 1);
            memmove (s - 1, s, len - 1);
            s[len - 2] = '\0';
            /* move beginning of next search */
            a = s;
            continue;
        }
        /* add this param */
        *s = '\0';
        debug (LEVEL_DEBUG, "param: %s\n", p);
        g_ptr_array_add (*params, (gpointer) p);
        /* move beginning of next one */
        p = s + 1;
        /* and move beginning of next search */
        a = p;
    }
    /* add the last param */
    debug (LEVEL_DEBUG, "param: %s\n", p);
    g_ptr_array_add (*params, (gpointer) p);
}

static void
free_plugins (void)
{
    GSList *l;
    destroy_fn destroy;

    debug (LEVEL_DEBUG, "closing plugins\n");
    for (l = plugins; l; l = l->next)
    {
        plugin_t *plugin = l->data;
        debug (LEVEL_DEBUG, "closing plugin %s\n", plugin->priv->file);

        debug (LEVEL_VERBOSE, "getting symbol plugin_destroy\n");
        if (G_UNLIKELY (!g_module_symbol (plugin->priv->module, "plugin_destroy",
                                          (gpointer *) &destroy)))
        {
            debug (LEVEL_DEBUG, "symbol destroy not found: %s\n",
                   g_module_error ());
        }
        else
        {
            if (G_UNLIKELY (destroy == NULL))
            {
                debug (LEVEL_DEBUG, "symbol destroy is NULL: %s\n",
                       g_module_error ());
            }
            else
            {
                destroy ();
            }
        }

        debug (LEVEL_VERBOSE, "closing module\n");
        if (G_UNLIKELY (!g_module_close (plugin->priv->module)))
        {
            debug (LEVEL_DEBUG, "unable to close module: %s", g_module_error());
        }
        
        g_free (plugin->priv->file);
        g_free (plugin->priv);
        g_free (plugin->info);
        g_free (plugin);
    }
    g_slist_free (plugins);
    plugins = NULL;
}

static void
free_memory (void)
{
    GSList *l;
    
    if (errors)
    {
        debug (LEVEL_DEBUG, "free-ing error messages\n");
        for (l = errors; l; l = l->next)
        {
            g_free (l->data);
        }
        g_slist_free (errors);
    }
    
    if (actions)
    {
        debug (LEVEL_DEBUG, "free-ing actions\n");
        g_hash_table_destroy (actions);
    }
    
    if (new_names)
    {
        debug (LEVEL_DEBUG, "free-ing list of new names\n");
        g_hash_table_destroy (new_names);
    }

    if (rules)
    {
        debug (LEVEL_DEBUG, "free-ing rules\n");
        g_hash_table_destroy (rules);
    }
    
    if (variables)
    {
        debug (LEVEL_DEBUG, "free-ing variables\n");
        g_hash_table_destroy (variables);
        g_hash_table_destroy (var_per_file);
        g_hash_table_destroy (var_global);
    }
    
    if (plugins)
    {
        free_plugins ();
    }
    
    if (curdir)
    {
        free (curdir);
    }
}

gboolean
add_var_value (const gchar *name, gchar *params, gchar *value)
{
    var_def_t *variable;
    gchar *s, *ss;
    size_t l1, l2;
    
    variable = g_hash_table_lookup (variables, (gpointer) name);
    if (!variable)
    {
        return FALSE;
    }
    
    l1 = strlen (name);
    l2 = strlen (params);
    ss = s = g_malloc ((l1 + l2 + 2) * sizeof (*s));
    memcpy (ss, name, l1);
    ss += l1;
    *ss++ = '/';
    memcpy (ss, params, l2);
    ss += l2;
    *ss = '\0';
    
    if (variable->type == VAR_TYPE_PER_FILE)
    {
        g_hash_table_insert (var_per_file, (gpointer) s, (gpointer) g_strdup (value));
    }
    else
    {
        g_hash_table_insert (var_global, (gpointer) s, (gpointer) g_strdup (value));
    }
    
    return TRUE;
}

static inline void
init_variables (void)
{
    init_vars_fn init_vars;
    var_def_t   *variable;
    GSList      *l;

    /* create hashmap of variables */
    variables = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                                        (GDestroyNotify) free_variable);

    /* create hashmap of cached values for per-file variables */
    var_per_file = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            (GDestroyNotify) g_free,
                                            (GDestroyNotify) g_free);

    /* create hashmap of cached values for global variables */
    var_global = g_hash_table_new_full (g_str_hash, g_str_equal,
                                        (GDestroyNotify) g_free,
                                        (GDestroyNotify) g_free);

    debug (LEVEL_DEBUG, "loading internal variables\n");
    variable = g_malloc0 (sizeof (*variable));

    variable->name = "NB";
    variable->description = "Counter, incremented for each file using it";
    variable->help = "You can specify up to 3 parameters:\n"
        "- the minimum number number of digits (padding with 0's)\n"
        "- the starting value of the counter\n"
        "- the increment (can be negative)\n"
        "E.g: $NB:3:42:-2$ will resolve as 042, 040, 038, etc";
    variable->type = VAR_TYPE_PER_FILE;
    variable->param = PARAM_SPLIT;
    variable->get_value = var_get_value_nb;
    add_var (variable);

    g_free (variable);

    debug (LEVEL_DEBUG, "loading variables from plugins\n");
    for (l = plugins; l; l = l->next)
    {
        plugin_t *plugin = l->data;

        debug (LEVEL_VERBOSE, "getting symbol plugin_init_vars\n");
        if (G_UNLIKELY (!g_module_symbol (plugin->priv->module, "plugin_init_vars",
                                            (gpointer *) &init_vars)))
        {
            debug (LEVEL_DEBUG, "skip module: symbol plugin_init_vars not found: %s\n",
                    g_module_error ());
            continue;
        }
        if (G_UNLIKELY (init_vars == NULL))
        {
            debug (LEVEL_DEBUG, "skip module: symbol plugin_init_vars is NULL: %s\n",
                    g_module_error ());
            continue;
        }

        debug (LEVEL_VERBOSE, "call plugin's init_vars\n");
        init_vars ();
    }
}
static gchar *
get_var_value (action_t *action, gchar var[255], guint len, GError **_error)
{
    GError     *local_err = NULL;
    gchar      *params;
    gchar      *value;
    var_def_t  *variable;
    GPtrArray  *arr = NULL;
    
    /* any params? */
    params = strchr (var, ':');
    if (!params)
    {
        params = &(var[len]);
        /* no params, we add a : for the key of cached values */
        var[len++] = ':';
        var[len] = '\0';
    }
    
    debug (LEVEL_VERBOSE, "looking up caches for: %s\n", var);

    /* do we have the value cached? */
    value = g_hash_table_lookup (var_per_file, (gpointer) var);
    if (value)
    {
        debug (LEVEL_VERBOSE, "found: %s\n", value);
        return value;
    }
    value = g_hash_table_lookup (var_global, (gpointer) var);
    if (value)
    {
        debug (LEVEL_VERBOSE, "found: %s\n", value);
        return value;
    }
    
    /* we only need the variable's name */
    *params = '\0';
    ++params;
    /* make sure such a variable exists then */
    debug (LEVEL_VERBOSE, "nothing cached, need definition for: %s\n", var);
    variable = g_hash_table_lookup (variables, (gpointer) var);
    if (!variable)
    {
        g_set_error (_error, MOLT_ERROR, 1, "unknown variable %s", var);
        return NULL;
    }
    /* params? */
    if (*params)
    {
        if (variable->param == PARAM_SPLIT)
        {
            split_params (':', params, &arr);
        }
        else
        {
            arr = g_ptr_array_new ();
            g_ptr_array_add (arr, (gpointer) params);
        }
    }
    /* ask for the value */
    debug (LEVEL_VERBOSE, "getting value for variable: %s -- params: %s\n",
           var, params);
    value = variable->get_value (action->file, arr, &local_err);
    if (arr)
    {
        g_ptr_array_free (arr, TRUE);
    }
    if (G_UNLIKELY (local_err))
    {
        g_set_error (_error, MOLT_ERROR, 1,
                     "unable to get value for variable %s: %s",
                     var, local_err->message);
        g_clear_error (&local_err);
        return NULL;
    }
    /* store it in the cache */
    add_var_value (var, params, value);
    debug (LEVEL_VERBOSE, "got: %s\n", value);
    return value;
}

static gboolean
parse_variables (action_t *action, gchar **new_name, GError **_error)
{
    GError      *local_err  = NULL;
    gchar       *s, *ss;
    gchar       *old;
    gchar       *name;
    size_t       l;
    size_t       len;
    size_t       alloc;
    gchar       *start      = NULL;
    gchar       *last;
    guint        i;
    gchar        buf[255];
    gchar       *value;
    
    debug (LEVEL_DEBUG, "parsing variables for: %s\n", action->new_name);
    
    /* make a copy of the new name, so we can modify it */
    old = g_strdup (action->new_name);
    
    /* init new name */
    len = 0;
    alloc = strlen (old) + 1024;
    name = g_malloc (alloc * sizeof (*name));
    
#define add_str(string) do {                                    \
        /* length of string to add */                           \
        l = strlen (string);                                    \
        /* realloc if needed */                                 \
        if (len + l >= alloc)                                   \
        {                                                       \
            alloc += l + 1024;                                  \
            name = g_realloc (name, alloc * sizeof (*name));    \
        }                                                       \
        /* add it */                                            \
        memcpy ((void *) &(name[len]), string, l);              \
        len += l;                                               \
    } while (0)
    
    for (s = last = old; *s; ++s)
    {
        /* found a variable marker? (must not be escaped) */
        if (*s == '$')
        {
            /* make sure it's not escaped */
            for (i = 1, ss = s - 1; ss >= old && *ss == '\\'; --ss, ++i)
                ;
            if (!(i % 2))
            {
                /* it's escaped, moving on */
                continue;
            }
            
            /* is it the start? */
            if (start == NULL)
            {
                start = s + 1;
            }
            /* ignore case of a %% */
            else if (s == start)
            {
                start = NULL;
            }
            /* then it's the end, and we can process it */
            else
            {
                /* we "end" the string here */
                *s = '\0';
                /* so start points to the variable name [and params] */
                i = (guint) snprintf (buf, 255, "%s", start);
                
                debug (LEVEL_VERBOSE, "need value for: %s\n", buf);
                
                /* get the value (handles params, cache, etc) */
                value = get_var_value (action, buf, i, &local_err);
                if (G_UNLIKELY (local_err))
                {
                    g_propagate_error (_error, local_err);
                    g_free (old);
                    g_free (name);
                    return FALSE;
                }
                
                /* replace the opening marked with a NULL, so we can add the
                 * string up to said marker */
                *(start - 1) = '\0';
                add_str (last);
                /* and add the value */
                add_str (value);
                
                /* reset */
                start = NULL;
                /* next */
                last = s + 1;
            }
        }
    }
    add_str (last);
    name[len] = '\0';
    
#undef add_str
    
    *new_name = name;
    return TRUE;
}

static option_t options[] = {
    { OPT_DEBUG,                "debug",
      "Enable debug mode - Specify twice for verbose\noutput" },
    { OPT_CONTINUE_ON_ERROR,    "continue-on-error",
      "Process as much as possible, even on errors\nor when conflicts are detected" },
    { OPT_DRY_RUN,              "dry-run",
      "Do not rename anything" },
    { OPT_EXCLUDE_DIRS,         "exclude-directories",
      "Ignore directories from specified files" },
    { OPT_EXCLUDE_FILES,        "exclude-files",
      "Ignore files from specified files" },
    { OPT_EXCLUDE_SYMLINKS,     "exclude-symlinks",
      "Ignore symlinks from specified files" },
    { OPT_OUTPUT_BOTH,          "output-both-names",
      "Output the old then the new filename for each file" },
    { OPT_OUTPUT_NEW,           "output-new-names",
      "Output the new filename for each file" },
    { OPT_ONLY_RULES,           "only-rules",
      "Only apply the rules and output results,\nwithout any conflict detection\n"
      "(Implies --dry-run)" },
    { OPT_PROCESS_FULLNAME,     "process-fullname",
      "Send the full path/name to the rules\n(Imply --output-fullname)" },
    { OPT_OUTPUT_FULLNAME,      "output-fullname",
      "Output full path/names" },
    { OPT_ALLOW_PATH,           "allow-path",
      "Allow (relative/absolute) paths in new filenames\n(Imply --output-fullname)" },
    { OPT_FROM_STDIN,           "from-stdin",
      "Get list of files from stdin" },
    { OPT_HELP,                 "help",
      "Show this help screen and exit - Specify twice for\nverbose output" },
    { OPT_VERSION,              "version",
      "Show version information and exit" },
};
static gint nb_options = sizeof (options) / sizeof (options[0]);

#define put_up_to_spaces(nb)    do {    \
        for (j = nb; j > 0; --j)        \
        {                               \
            fputc (' ', stdout);        \
        }                               \
    } while (0)

#define put_string(help, nb)  do {        \
        for (s = help; *s; ++s)         \
        {                               \
            fputc (*s, stdout);         \
            if (*s == '\n')             \
            {                           \
                /* auto-add spaces on   \
                 * new line */          \
                put_up_to_spaces (nb);  \
            }                           \
        }                               \
        fputc ('\n', stdout);           \
    } while (0)

#define put_list(types, type, prefix, str_param)   do {                     \
        g_hash_table_iter_init (&iter, types);                              \
        while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &type))    \
        {                                                                   \
            j = fprintf (stdout, prefix "%s%s", type->name,                 \
                        (type->param != PARAM_NONE) ? str_param : "");      \
            put_up_to_spaces (30 - j);                                      \
            fputs (type->description, stdout);                              \
            fputc ('\n', stdout);                                           \
            if (verbose && type->help)                                      \
            {                                                               \
                fputs ("   ", stdout);                                      \
                put_string (type->help, 3);                                   \
                fputc ('\n', stdout);                                       \
            }                                                               \
        }                                                                   \
    } while (0)

static void
show_help (gboolean verbose)
{
    gint            i, j;
    option_t        *opt;
    const gchar     *s;
    GHashTableIter   iter;
    rule_def_t      *rule;
    var_def_t       *variable;
    
    fputs ("Renames specified files by applying specified rules\n", stdout);
    fputs ("Usage: molt [OPTION]... RULE... [FILE]...\n", stdout);
    fputc ('\n', stdout);
    
    fputs ("\tOptions :\n", stdout);
    for (i = 0; i < nb_options; ++i)
    {
        opt = &options[i];
        j = fprintf (stdout, " -%c, --%s", opt->opt_short, opt->opt_long);
        put_up_to_spaces (30 - j);
        put_string (opt->help, 31);
    }
    
    fputs ("\n\tRules :\n", stdout);
    if (verbose)
    {
        fputs ("Rules are the part of molt that process filenames. Rules will be applied\n"
            "in the order specified, you can use the same rule as may times as you want.\n"
            "Some rules require a parameter, what it can be depend of the rule. Usually,\n"
            "it will be a string where you can specify multiple parameter using slash ( / )\n"
            "as separator.\n", stdout);
        fputc ('\n', stdout);
    }
    put_list (rules, rule, " --", " PARAM");
    
    init_variables ();
    fputs ("\n\tVariables :\n", stdout);
    if (verbose)
    {
        fputs ("You can use variables in the new filenames. The syntax is to put the\n"
            "variable's name in between dollar signs, e.g: $FOOBAR$\n"
            "You can also (if supported) specify one (or more) parameters, using colon\n"
            "as separator, e.g: $FOOBAR:PARAM1:PARAM2$\n"
            "Variables are not automatically resolved, you need to use the rule --vars\n"
            "in order to have them resolved, which gives you the ability to determine\n"
            "when resolving happens, and continue processing with more rules afterwards.\n"
            "Note that rule --tpl also resolves variables.\n", stdout);
        fputc ('\n', stdout);
    }
    put_list (variables, variable, " ", "[:PARAM...]");
    
    exit (0);
}

#undef put_list

static void
show_version (void)
{
    GSList      *l;
    gint         j;
    const gchar *s;
    plugin_t    *plugin;
    
    fputs ("molt [batch renaming utility] version " APP_VERSION "\n", stdout);
    fputs ("Copyright (C) 2012 Olivier Brunel\n", stdout);
    fputs ("License GPLv3+: GNU GPL version 3 or later "
           "<http://gnu.org/licenses/gpl.html>\n", stdout);
    fputs ("This is free software: you are free to change and redistribute it.\n", stdout);
    fputs ("There is NO WARRANTY, to the extent permitted by law.\n", stdout);
    
    if (plugins)
    {
        fputs ("\n\tPlugins :\n", stdout);
        for (l = plugins; l; l = l->next)
        {
            plugin = l->data;
            fputs ("- ", stdout);
            fputs (plugin->info->name, stdout);
            fputs (" [", stdout);
            if (G_LIKELY (NULL != (s = strrchr (plugin->priv->file, '/'))))
            {
                ++s;
            }
            else
            {
                s = plugin->priv->file;
            }
            fputs (s, stdout);
            fputs ("] version ", stdout);
            fputs (plugin->info->version, stdout);
            fputs ("; By ", stdout);
            fputs (plugin->info->author, stdout);
            fputc ('\n', stdout);
            put_string (plugin->info->description, 1);
            fputc ('\n', stdout);
        }
    }
    
    exit (0);
}

#undef put_string
#undef put_up_to_spaces

static gboolean
process_arg (int argc, char *argv[], gint *argi, gchar **option)
{
    static gchar *c = NULL;
    gint i;
    
    *option = NULL;
    for (; *argi < argc; ++*argi)
    {
        debug (LEVEL_VERBOSE, "processing argi=%d\n", *argi);
        
        /* starts with a dash? */
        if (argv[*argi][0] == '-')
        {
            /* long option or rule? */
            if (argv[*argi][1] == '-')
            {
                /* first, check if it is "--" */
                if (argv[*argi][2] == '\0')
                {
                    ++*argi;
                    debug (LEVEL_VERBOSE, "found -- separator\n");
                    return FALSE;
                }
                /* then check the options */
                for (i = 0; i < nb_options; ++i)
                {
                    if (0 == strcmp (argv[*argi] + 2, options[i].opt_long))
                    {
                        debug (LEVEL_DEBUG, "long option: --%s\n", options[i].opt_long);
                        *option = &options[i].opt_short;
                        ++*argi;
                        return TRUE;
                    }
                }
                /* must be a rule then*/
                return TRUE;
            }
            /* short option then */
            else
            {
                if (!c)
                {
                    c = argv[*argi];
                    /* if first option is -d|d] it's already been processed */
                    if (*argi == 1)
                    {
                        if (*(c + 1) == OPT_DEBUG)
                        {
                            ++c;
                            if (*(c + 1) == OPT_DEBUG)
                            {
                                ++c;
                            }
                        }
                    }
                }
                for (++c; *c; ++c)
                {
                    for (i = 0; i < nb_options; ++i)
                    {
                        if (*c == options[i].opt_short)
                        {
                            debug (LEVEL_DEBUG, "short option: -%c (--%s)\n",
                                   *c, options[i].opt_long);
                            *option = &options[i].opt_short;
                            return TRUE;
                        }
                    }
                    *option = c;
                    debug (LEVEL_VERBOSE, "unknown option -%c\n", *c);
                    return FALSE;
                }
                c = NULL;
                continue;
            }
        }
        /* must be the first file */
        else
        {
            debug (LEVEL_VERBOSE, "first file?\n");
            return FALSE;
        }
    }
    debug (LEVEL_VERBOSE, "no more args\n");
    return FALSE;
}

static inline void
show_output (output_t output, gint state, action_t *action, gchar *name)
{
    gchar *file = (output_fullname) ? action->file : action->filename;
    gchar *new;
    
    /* might might be new_name if we renamed */
    if (name == action->new_name)
    {
        new = (output_fullname) ? action->new_name : action->new_filename;
    }
    /* or it might be file if there's no rename (whatever the reason) */
    else if (name == action->file)
    {
        new = file;
    }
    /* or it could be tmp_name if the 2nd renaming step failed */
    else /* if (name == action->tmp_name) */
    {
        if (output_fullname)
        {
            new = name;
        }
        else
        {
            new = strrchr (name, '/');
            ++new;
        }
    }
    
    /* if there was no success AND there is a tmp_name it can only mean one
     * thing: the second rename (tmp -> new) just failed */
    if (G_UNLIKELY (state != 0 && action->tmp_name))
    {
        fprintf (stderr, "%s is now %s\n", action->file, action->tmp_name);
    }
    
    switch (output)
    {
        case OUTPUT_STANDARD:
            if (state == 0)
            {
                fprintf (stdout, "%s -> %s\n", file, new);
            }
            break;
        case OUTPUT_BOTH_NAMES:
            fprintf (stdout, "%s\n", file);
            /* fall through */
        case OUTPUT_NEW_NAMES:
            fprintf (stdout, "%s\n", new);
            break;
    }
}

static gchar *
get_tmp_name (const gchar *name)
{
    gchar buf[15];
    FILE *fp;
    int c;
    gint i = 0;
    
    fp = fopen ("/dev/urandom", "r");
    if (!fp)
    {
        return NULL;
    }
    
    buf[i++] = '_';
    buf[i++] = 'm';
    buf[i++] = 'o';
    buf[i++] = 'l';
    buf[i++] = 't';
    buf[i++] = '_';
    for ( ; i < 14; ++i)
    {
        c = (fgetc (fp) % 26);
        c += 'a';
        buf[i] = (gchar) c;
    }
    buf[i] = '.';
    buf[15] = '\0';
    
    fclose (fp);
    return g_strconcat (buf, name, NULL);
}

#define action_error(...)   do {                        \
    if (nb_two_steps == 0)                              \
    {                                                   \
        fprintf (stderr, __VA_ARGS__);                  \
    }                                                   \
    else                                                \
    {                                                   \
        action->error = g_strdup_printf (__VA_ARGS__);  \
    }                                                   \
} while (0)

static void
free_commands (GSList *commands)
{
    GSList *l;
    
    debug (LEVEL_DEBUG, "free-ing commands\n");
    for (l = commands; l; l = l->next)
    {
        command_t *command = l->data;
        debug (LEVEL_VERBOSE, "free-ing command for rule %s\n", command->rule->name);
        if (command->rule->destroy)
        {
            command->rule->destroy (&(command->data));
        }
        g_slice_free (command_t, command);
    }
    g_slist_free (commands);
}

static void
add_action_for_file (gchar *file, GFileTest test_types,
                     GSList *commands, GSList **actions_list)
{
    GError      *local_err = NULL;
    static guint cur = 0;
    action_t    *action;
    command_t   *command;
    gchar       *new_name;
    GSList      *l;
    gboolean     has_parsed_variables = FALSE;
    
    if (!g_file_test (file, G_FILE_TEST_EXISTS))
    {
        error (ERROR_FILE, "file does not exist: %s\n", file);
        return;
    }
    if (test_types == (G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_DIR
                        | G_FILE_TEST_IS_SYMLINK))
    {
        debug (LEVEL_DEBUG, "process: %s\n", file);
    }
    else if (test_types & G_FILE_TEST_IS_REGULAR
        && g_file_test (file, G_FILE_TEST_IS_REGULAR))
    {
        debug (LEVEL_DEBUG, "process file: %s\n", file);
    }
    else if (test_types & G_FILE_TEST_IS_DIR
        && g_file_test (file, G_FILE_TEST_IS_DIR))
    {
        debug (LEVEL_DEBUG, "process dir: %s\n", file);
    }
    else if (test_types & G_FILE_TEST_IS_SYMLINK
        && g_file_test (file, G_FILE_TEST_IS_SYMLINK))
    {
        debug (LEVEL_DEBUG, "process symlink: %s\n", file);
    }
    else
    {
        debug (LEVEL_DEBUG, "ignore: %s\n", file);
        return;
    }
    
    /* create new action */
    action = g_slice_new0 (action_t);
    action->cur = ++cur;
    set_full_file_name (file, &(action->file), &(action->filename));
    /* make sure we have a filename */
    if (*action->filename == '\0')
    {
        error (ERROR_SYNTAX, "%s: no filename\n", action->file);
        free_action (action);
        --cur;
        return;
    }
    /* make sure there isn't already an action for this file */
    if (g_hash_table_lookup (actions, (gpointer) action->file))
    {
        debug (LEVEL_DEBUG, "already an action for this file, aborting\n");
        free_action (action);
        --cur;
        return;
    }
    /* put in the new name a copy of the current one. this will be free-d
     * and updated after each rule that does provide a new name */
    action->new_name = g_strdup ((process_fullname) ? action->file : action->filename);
    
    /* run rules and get the new name */
    new_name = NULL;
    for (l = commands; l; l = l->next)
    {
        command = l->data;
        debug (LEVEL_DEBUG, "running rule %s on %s\n", command->rule->name,
               action->new_name);
        if (G_LIKELY (command->rule->run (&(command->data),
                                            action->new_name,
                                            &new_name,
                                            &local_err)))
        {
            /* did we get a new name? */
            if (new_name)
            {
                debug (LEVEL_VERBOSE, "new name: %s\n", new_name);
                g_free (action->new_name);
                action->new_name = new_name;
                new_name = NULL;
            }
            /* should we parse variables? */
            if (command->rule->parse_variables)
            {
                debug (LEVEL_DEBUG, "parsing variables\n");
                has_parsed_variables = TRUE;
                if (G_LIKELY (parse_variables (action, &new_name, &local_err)))
                {
                    debug (LEVEL_VERBOSE, "new name: %s\n", new_name);
                    g_free (action->new_name);
                    action->new_name = new_name;
                    new_name = NULL;
                }
                else
                {
                    error (ERROR_RULE_FAILED, "%s: failed to parse variables: %s\n",
                           action->file, local_err->message);
                    g_clear_error (&local_err);
                    /* we can't continue processing this action now */
                    g_free (action->new_name);
                    action->new_name = NULL;
                    break;
                }
            }
        }
        else
        {
            error (ERROR_RULE_FAILED, "%s: rule %s failed: %s\n",
                   action->file, command->rule->name, local_err->message);
            g_clear_error (&local_err);
            /* we can't continue processing this action now */
            g_free (action->new_name);
            action->new_name = NULL;
            break;
        }
    }
    debug (LEVEL_DEBUG, "all commands applied\n");
    if (has_parsed_variables)
    {
        /* clear cache of per-file values */
        g_hash_table_destroy (var_per_file);
        /* recreates an empty hashmap */
        var_per_file = g_hash_table_new_full (g_str_hash, g_str_equal,
                                              (GDestroyNotify) g_free,
                                              (GDestroyNotify) g_free);
    }
    /* check whether we actually have a new name or not */
    if (action->new_name && g_strcmp0 (action->new_name,
            (process_fullname) ? action->file : action->filename) != 0)
    {
        /* check validity of new name */
        gboolean is_valid = (strlen (action->new_name) > 0);
        
        if (is_valid)
        {
            if (process_fullname)
            {
                /* must be a full path*/
                is_valid = action->new_name[0] == '/';
            }
            else if (!allow_path)
            {
                is_valid = (NULL == strchr (action->new_name, '/'));
            }
        }
        
        if (!is_valid)
        {
            error (ERROR_INVALID_NAME, "%s: invalid new name: %s\n",
                   action->file, action->new_name);
            g_free (action->new_name);
            action->new_name = NULL;
        }
        else
        {
            /* set the fullname and pointer to the filename */
            if (action->new_name[0] == '/')
            {
                new_name = action->new_name;
            }
            else
            {
                gchar *s;
                /* because the action isn't necessarily for one in curdir, we
                 * need to prefix it with its own path.
                 * action->filename is a pointer to the first char of the
                 * filename inside action->file, so we turn the / before into
                 * a NULL to get the path, and then restore it. */
                s = action->filename - 1;
                *s = '\0';
                new_name = g_strconcat (action->file, "/", action->new_name, NULL);
                *s = '/';
                g_free (action->new_name);
            }
            set_full_file_name (new_name, &(action->new_name), &(action->new_filename));
            g_free (new_name);
            
            debug (LEVEL_DEBUG, "new name: %s\n", action->new_name);
            set_to_rename (action, action);
        }
    }
    else
    {
        debug (LEVEL_DEBUG, "no new name\n");
        /* no new name, we can free this */
        g_free (action->new_name);
        action->new_name = NULL;
    }
    /* add action to hashmap (for easy access) */
    g_hash_table_insert (actions, (gpointer) action->file, (gpointer) action);
    /* and in list, to preserve order (when processing) */
    *actions_list = g_slist_append (*actions_list, (gpointer) action);
}

#define do_rename(old_name, new_name)   do {                            \
        debug (LEVEL_DEBUG, "renaming %s to %s\n", old_name, new_name); \
        if (G_UNLIKELY (0 != (state = rename (old_name, new_name))))    \
        {                                                               \
            action_error ("%s: failed to rename to %s: %s\n",           \
                        action->file, new_name, strerror (errno));      \
        }                                                               \
    } while (0)

int
main (int argc, char **argv)
{
    GError        *local_err = NULL;
    gint           argi  = 1;
    rule_def_t    *rule;
    
    GDir          *dir;
    const gchar   *filename;
    gchar         *file;
    
    GSList        *commands = NULL;
    command_t     *command;
    GPtrArray     *ptr_arr;
    guint          i;
    gboolean       do_parse_variables = FALSE;
    
    gchar         *option;
    GFileTest      test_types = G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_DIR
                                | G_FILE_TEST_IS_SYMLINK;
    gboolean       continue_on_error = FALSE;
    gboolean       dry_run           = FALSE;
    output_t       output            = OUTPUT_STANDARD;
    gboolean       only_rules        = FALSE;
    gboolean       from_stdin        = FALSE;
    
    GSList        *actions_list      = NULL;
    action_t      *action;
    GSList        *l;
    
    /* try to get debug option now so it applies to loading rules as well.
     * Note: only works if the first option is -d[d] (--debug not supported) */
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == OPT_DEBUG)
    {
        ++level;
        if (argv[1][2] == OPT_DEBUG)
        {
            ++level;
        }
        debug (LEVEL_DEBUG, "debug level: %d\n", level);
    }
    
    /* create hashmap of rules */
    rules = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                                   (GDestroyNotify) free_rule);
    
    debug (LEVEL_DEBUG, "adding internal rules\n");
    rule = g_malloc0 (sizeof (*rule));
    
    rule->name = "lower";
    rule->description = "Convert to lowercase";
    rule->help = NULL;
    rule->param = PARAM_NONE;
    rule->init = NULL;
    rule->run = rule_to_lower;
    rule->destroy = NULL;
    rule->parse_variables = FALSE;
    add_rule (rule);
    
    rule->name = "upper";
    rule->description = "Convert to uppercase";
    rule->help = NULL;
    rule->param = PARAM_NONE;
    rule->init = NULL;
    rule->run = rule_to_upper;
    rule->destroy = NULL;
    rule->parse_variables = FALSE;
    add_rule (rule);
    
    rule->name = "camel";
    rule->description = "Convert to Camel Case";
    rule->help = NULL;
    rule->param = PARAM_NONE;
    rule->init = NULL;
    rule->run = rule_camel;
    rule->destroy = NULL;
    rule->parse_variables = FALSE;
    add_rule (rule);
    
    rule->name = "sr";
    rule->description = "Search & replace a string";
    rule->help = "PARAM = search[/replacement[/option]]\n"
        "If no replacement is specified, the string will be removed.\n"
        "Search is case-sensitive, unless option i was specified.";
    rule->param = PARAM_SPLIT;
    rule->init = rule_sr_init;
    rule->run = (rule_run_fn) rule_sr;
    rule->destroy = rule_sr_destroy;
    rule->parse_variables = FALSE;
    add_rule (rule);
    
    rule->name = "list";
    rule->description = "Use list of new names from stdin";
    rule->help = NULL;
    rule->param = PARAM_NONE;
    rule->init = rule_list_init;
    rule->run = rule_list;
    rule->destroy = NULL;
    rule->parse_variables = FALSE;
    add_rule (rule);
    
    rule->name = "regex";
    rule->description = "Search & replace using regular expression";
    rule->help = "PARAM = pattern[/replacement[/option]]\n"
        "If no replacement is specified, the string will be removed.\n"
        "Search is case-sensitive, unless option i was specified.";
    rule->param = PARAM_SPLIT;
    rule->init = rule_regex_init;
    rule->run = rule_regex;
    rule->destroy = rule_regex_destroy;
    rule->parse_variables = FALSE;
    add_rule (rule);
    
    rule->name = "vars";
    rule->description = "Parse variables";
    rule->help = NULL;
    rule->param = PARAM_NONE;
    rule->init = NULL;
    rule->run = rule_variables;
    rule->destroy = NULL;
    rule->parse_variables = TRUE;
    add_rule (rule);
    
    rule->name = "tpl";
    rule->description = "Apply specified template (parse variables)";
    rule->help = NULL;
    rule->param = PARAM_NO_SPLIT;
    rule->init = rule_tpl_init;
    rule->run = rule_tpl;
    rule->destroy = NULL;
    rule->parse_variables = TRUE;
    add_rule (rule);
    
    g_free (rule);
    
#define close_module(free_struct)  do {                             \
        if (free_struct)                                            \
        {                                                           \
            g_free (plugin->info);                                  \
            g_free (plugin->priv);                                  \
            g_free (plugin);                                        \
        }                                                           \
        if (G_UNLIKELY (!g_module_close (module)))                  \
        {                                                           \
            debug (LEVEL_VERBOSE, "unable to close plugin: %s\n",   \
                    g_module_error ());                             \
        }                                                           \
    } while (0)
    
#define get_symbol(symbol_name, symbol_fn, free_struct) do {                    \
        debug (LEVEL_VERBOSE, "getting symbol " symbol_name "\n");              \
        if (G_UNLIKELY (!g_module_symbol (module, symbol_name,                  \
                                          (gpointer *) &symbol_fn)))            \
        {                                                                       \
            debug (LEVEL_DEBUG, "symbol " symbol_name " not found in %s: %s\n", \
                   file, g_module_error ());                                    \
            close_module (free_struct);                                         \
            continue;                                                           \
        }                                                                       \
        if (G_UNLIKELY (symbol_name == NULL))                                   \
        {                                                                       \
            debug (LEVEL_DEBUG, "symbol " symbol_name " is NULL in %s: %s\n",   \
                   file, g_module_error ());                                    \
            close_module (free_struct);                                         \
            continue;                                                           \
        }                                                                       \
    } while (0)
    
    debug (LEVEL_DEBUG, "loading plugins from %s\n", PLUGINS_PATH);
    if (!(dir = g_dir_open (PLUGINS_PATH, 0, &local_err)))
    {
        debug (LEVEL_DEBUG, "cannot load plugins: unable to open %s: %s\n",
               PLUGINS_PATH, local_err->message);
        g_clear_error (&local_err);
    }
    else
    {
        GModule           *module;
        plugin_t          *plugin, **molt_plugin;
        gint               req_api;
        check_version_fn   check_version;
        init_fn            init;
        set_info_fn        set_info;
        plugin_functions_t functions = {
            &debug,
            &get_stdin,
            &add_rule,
            &add_var,
            &add_var_value
        };
        
        while ((filename = g_dir_read_name (dir)))
        {
            debug (LEVEL_VERBOSE, "opening plugin: %s\n", filename);
            file = g_strconcat (PLUGINS_PATH, filename, NULL);
            module = g_module_open (file, G_MODULE_BIND_LAZY);
            if (G_UNLIKELY (!module))
            {
                debug (LEVEL_DEBUG, "cannot open plugin %s: %s\n",
                       file, g_module_error ());
                continue;
            }
            
            get_symbol ("plugin_check_version", check_version, FALSE);
            debug (LEVEL_VERBOSE, "call plugin's check_version\n");
            req_api = check_version (MOLT_ABI_VERSION);
            if (req_api == -1)
            {
                debug (LEVEL_DEBUG, "plugin requires more recent ABI\n");
                close_module (FALSE);
                continue;
            }
            else if (req_api > MOLT_API_VERSION)
            {
                debug (LEVEL_DEBUG, "plugin requires more recent API");
                close_module (FALSE);
                continue;
            }
            
            /* create the plugin struct */
            plugin = g_new0 (plugin_t, 1);
            plugin->info = g_new0 (plugin_info_t, 1);
            plugin->functions = &functions;
            plugin->priv = g_new0 (plugin_priv_t, 1);
            
            debug (LEVEL_VERBOSE, "getting symbol molt_plugin\n");
            if (G_UNLIKELY (!g_module_symbol (module, "molt_plugin",
                                              (gpointer *) &molt_plugin)))
            {
                debug (LEVEL_DEBUG, "symbol molt_plugin not found in %s: %s\n",
                       file, g_module_error ());
                close_module (TRUE);
                continue;
            }
            if (molt_plugin == NULL)
            {
                debug (LEVEL_DEBUG, "symbol molt_plugin is NULL in %s: %s\n",
                       file, g_module_error ());
                close_module (TRUE);
                continue; 
            }
            debug (LEVEL_VERBOSE, "setting plugin's molt_plugin\n");
            *molt_plugin = plugin;
            
            get_symbol ("plugin_set_info", set_info, TRUE);
            debug (LEVEL_VERBOSE, "call plugin's set_info\n");
            set_info (plugin->info);
            
            get_symbol ("plugin_init", init, TRUE);
            debug (LEVEL_VERBOSE, "call plugin's init\n");
            init ();
            
            /* store ref to this plugin */
            plugin->priv->file = g_strdup (file);
            plugin->priv->module = module;
            debug (LEVEL_DEBUG, "adding plugin %s\n", file);
            plugins = g_slist_prepend (plugins, plugin);
        }
        debug (LEVEL_DEBUG, "closing folder\n");
        g_dir_close (dir);
    }
#undef get_symbol
#undef close_module
    
    gint help = 0;
    gint version = 0;
    debug (LEVEL_DEBUG, "process options/rules, i=%d\n", argi);
    while (process_arg (argc, argv, &argi, &option))
    {
        if (option)
        {
            switch (*option)
            {
                case OPT_DEBUG:
                    ++level;
                    debug (LEVEL_DEBUG, "debug level: %d\n", level);
                    break;
                case OPT_CONTINUE_ON_ERROR:
                    continue_on_error = TRUE;
                    break;
                case OPT_DRY_RUN:
                    dry_run = TRUE;
                    break;
                case OPT_EXCLUDE_DIRS:
                    test_types ^= G_FILE_TEST_IS_DIR;
                    break;
                case OPT_EXCLUDE_FILES:
                    test_types ^= G_FILE_TEST_IS_REGULAR;
                    break;
                case OPT_EXCLUDE_SYMLINKS:
                    test_types ^= G_FILE_TEST_IS_SYMLINK;
                    break;
                case OPT_OUTPUT_BOTH:
                    output = OUTPUT_BOTH_NAMES;
                    break;
                case OPT_OUTPUT_NEW:
                    output = OUTPUT_NEW_NAMES;
                    break;
                case OPT_OUTPUT_FULLNAME:
                    output_fullname = TRUE;
                    break;
                case OPT_ONLY_RULES:
                    only_rules = TRUE;
                    debug (LEVEL_DEBUG, "implied option --dry-run\n");
                    dry_run = TRUE;
                    break;
                case OPT_PROCESS_FULLNAME:
                    process_fullname = TRUE;
                    debug (LEVEL_DEBUG, "implied option --output-fullname\n");
                    output_fullname = TRUE;
                    break;
                case OPT_ALLOW_PATH:
                    allow_path = TRUE;
                    debug (LEVEL_DEBUG, "implied option --output-fullname\n");
                    output_fullname = TRUE;
                    break;
                case OPT_FROM_STDIN:
                    from_stdin = TRUE;
                    break;
                case OPT_HELP:
                    ++help;
                    break;
                case OPT_VERSION:
                    ++version;
                    break;
            }
        }
        else
        {
            rule = g_hash_table_lookup (rules, argv[argi] + 2);
            if (G_UNLIKELY (!rule))
            {
                error (ERROR_SYNTAX, "unknown rule: %s\n", argv[argi] + 2);
                ++argi;
                continue;
            }
            if (G_UNLIKELY (rule->param != PARAM_NONE && argi + 1 >= argc))
            {
                error (ERROR_SYNTAX, "missing parameter for rule %s\n", rule->name);
                ++argi;
                continue;
            }
            debug (LEVEL_DEBUG, "adding new command, rule: %s\n", rule->name);
            command = g_slice_new0 (command_t);
            command->rule = rule;
            /* if there's an init, we deal with it & params */
            if (rule->init)
            {
                if (rule->param == PARAM_SPLIT)
                {
                    split_params ('/', argv[++argi], &ptr_arr);
                }
                else if (rule->param == PARAM_NO_SPLIT)
                {
                    ptr_arr = g_ptr_array_new ();
                    g_ptr_array_add (ptr_arr, argv[++argi]);
                }
                else /* if (rule->param == PARAM_NONE) */
                {
                    ptr_arr = NULL;
                }
                if (G_UNLIKELY (level && ptr_arr))
                {
                    debug (LEVEL_DEBUG, "command has %u params\n", ptr_arr->len);
                    for (i = 0; i < ptr_arr->len; ++i)
                    {
                        debug (LEVEL_VERBOSE, "param#%d: %s\n", i + 1,
                               g_ptr_array_index (ptr_arr, i));
                    }
                }
                /* run init */
                if (G_UNLIKELY (!rule->init (&(command->data),
                                             ptr_arr,
                                             &local_err)))
                {
                    error (ERROR_RULE_FAILED, "Unable to initialize rule %s: %s\n",
                           command->rule->name, local_err->message);
                    g_clear_error (&local_err);
                    g_slice_free (command_t, command);
                    command = NULL;
                }
                /* we don't keep the array. if commands/rules need to keep
                * params, it's up to them through their data
                * Note: the actual pointers are kept/can be used by rules */
                if (ptr_arr)
                {
                    g_ptr_array_free (ptr_arr, TRUE);
                }
            }
            /* add command */
            if (command)
            {
                commands = g_slist_append (commands, command);
                if (command->rule->parse_variables)
                {
                    do_parse_variables = TRUE;
                }
            }
            
            ++argi;
        }
    }
    if (option)
    {
        error (ERROR_SYNTAX, "unknown option: %c\n", *option);
    }
    
    /* show errors if any, exit unless continue-on-error is set */
    if (errors)
    {
        if (commands)
        {
            free_commands (commands);
        }
        /* ERROR_SYNTAX means invalid option or something, so we bail out
         * even with continue-on-error
         * ERROR_RULE_FAILED is pretty much the same, only maybe worse, as it
         * means a command/rule could not be init, and was ignored! */
        error_out (!continue_on_error || err & (ERROR_RULE_FAILED | ERROR_SYNTAX));
    }
    
    /* help */
    if (help)
    {
        show_help (help > 1); /* calls exit() */
    }
    
    /* version */
    if (version)
    {
        show_version (); /* calls exit() */
    }
    
    /* make sure we have something to do */
    if (!commands)
    {
        error (ERROR_SYNTAX, "nothing to do: no rules to be applied\n");
        error_out (TRUE);
    }
    
    /* create hashmap of actions */
    actions = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                                     (GDestroyNotify) free_action);
    
    /* create hashmap of new names */
    new_names = g_hash_table_new (g_str_hash, g_str_equal);
    
    /* get curdir */
    if (!(curdir = getcwd (NULL, 0)))
    {
        if (commands)
        {
            free_commands (commands);
        }
        error (ERROR_NONE, "Unable to get current path\n");
        /* this is an error we can't ignore */
        error_out (TRUE);
    }
    
    /* do we need variables? */
    if (do_parse_variables)
    {
        init_variables ();
    }
    
    if (from_stdin)
    {
        FILE  *stream;
        char   buf[4096];
        size_t len;
        
        debug (LEVEL_DEBUG, "process file names from stdin\n");
        
        /* there mustn't be anything else on command-line */
        if (argi < argc)
        {
            for (; argi < argc; ++argi)
            {
                error (ERROR_SYNTAX, "invalid argument: %s\n", argv[argi]);
            }
            error_out (TRUE);
        }
        
        if (!get_stdin ((gpointer *) &stream, &local_err))
        {
            error (ERROR_NONE, "unable to get stdin: %s\n", local_err->message);
            g_clear_error (&local_err);
            error_out (TRUE);
        }
        
        while (fgets ((char *)&buf, 4096, stream))
        {
            /* reading from stdin, filenames might end with a \n to strip */
            len = strlen (buf);
            if (buf[len - 1] == '\n')
            {
                --len;
                buf[len] = '\0';
            }
            if (len > 0)
            {
                add_action_for_file (buf, test_types, commands, &actions_list);
            }
        }
    }
    else
    {
        debug (LEVEL_DEBUG, "process file names from args, i=%d\n", argi);
        for ( ; argi < argc; ++argi)
        {
            add_action_for_file (argv[argi], test_types, commands, &actions_list);
        }
    }
    free_commands (commands);
    if (do_parse_variables)
    {
        /* clear cache of per-file values */
        g_hash_table_destroy (var_per_file);
        /* clear cache of global values */
        g_hash_table_destroy (var_global);
        /* we're done with variables */
        g_hash_table_destroy (variables);
        variables = NULL;
    }
    free_plugins ();
    
    /* show errors if any, exit unless continue-on-error is set */
    if (errors)
    {
        /* ERROR_RULE_FAILED means a rule could not ran (e.g. syntax error in
         * its params or something) and the new name couldn't be established,
         * which could also have impact in terms of conflicts and such.
         * So we bail out even with continue-on-error */
        error_out (!continue_on_error || err & ERROR_RULE_FAILED);
    }
    
    if (G_UNLIKELY (!actions_list))
    {
        /* i.e. nothing was specified on command-line, hence ERROR_SYNTAX */
        error (ERROR_SYNTAX, "nothing to do: no files to rename\n");
        error_out (TRUE);
    }
    
    gint state;
    gchar *name;
    
    /* process actions: rename files & construct output */
    for (l = actions_list; l; l = l->next)
    {
        action = l->data;
        name = NULL;
        state = -1;
        
        if (action->state & ST_TO_RENAME)
        {
            /* only rename if we "can" do the rename, i.e. either there was no
             * conflicts found, or continue-on-error is set */
            if (nb_conflicts == 0 || continue_on_error)
            {
                if (!dry_run)
                {
                    if (action->state & ST_TWO_STEPS)
                    {
                        action->tmp_name = get_tmp_name (action->new_name);
                        name = action->tmp_name;
                    }
                    else
                    {
                        name = action->new_name;
                    }
                    
                    /* name could be NULL if get_tmp_name() somehow failed */
                    if (G_LIKELY (name))
                    {
                        do_rename (action->file, name);
                    }
                    else
                    {
                        state = -1;
                    }
                    
                    if (G_UNLIKELY (state != 0))
                    {
                        /* do_rename took care of the error message */
                        if (action->tmp_name)
                        {
                            g_free (action->tmp_name);
                            action->tmp_name = NULL;
                        }
                        name = action->file;
                        /* remove the to-rename state so that in case of a
                         * second pass (if nb_two_steps > 0) it isn't seen
                         * as to-rename and therefore marked as success */
                        action->state &= ~ST_TO_RENAME;
                    }
                }
                else
                {
                    name = action->new_name;
                    state = 0;
                }
            }
        }
        else if (action->state & ST_CONFLICT)
        {
            err |= ERROR_CONFLICT_RENAME;
            action_error ("%s: cannot be renamed, conflict\n", action->file);
        }
        else if (action->state & ST_CONFLICT_FS)
        {
            err |= ERROR_CONFLICT_FS;
            action_error ("%s: cannot be renamed, new name (%s) in use\n",
                          action->file, action->new_name);
        }
        if (!name)
        {
            if (only_rules && action->new_name)
            {
                name = action->new_name;
            }
            else
            {
                name = action->file;
            }
        }
        /* output can be shown if no two-steps renaming are required, else
         * we prepare it but only show it once everything was processed, in
         * in order to be accurate and keep the order as expected */
        if (nb_two_steps == 0)
        {
            show_output (output, state, action, name);
        }
    }
    
    /* if there were two-steps renaming, we need to finish them & show output */
    if (nb_two_steps > 0)
    {
        for (l = actions_list; l; l = l->next)
        {
            action = l->data;
            state = -1;
            name = action->new_name;
            
            if (action->tmp_name)
            {
                do_rename (action->tmp_name, name);
                if (G_UNLIKELY (state != 0))
                {
                    /* do_rename took care of the error message */
                    name = action->tmp_name;
                }
            }
            else if (action->state & ST_TO_RENAME)
            {
                state = 0;
            }
            
            if (action->error)
            {
                fprintf (stderr, "%s", action->error);
                g_free (action->error);
                action->error = NULL;
            }
            show_output (output, state, action, name);
            
            if (action->tmp_name)
            {
                g_free (action->tmp_name);
                action->tmp_name = NULL;
            }
        }
    }
    
    if (err)
    {
        error_out (TRUE);
    }
    
    free_memory ();
    return err; /* err == 0 */
}
#undef do_rename
