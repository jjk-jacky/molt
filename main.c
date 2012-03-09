
/* C */
#include <stdio.h>
#include <stdlib.h> /* exit */
#include <string.h>
#include <time.h> /* for debug() */
#include <unistd.h> /* getcwd */
#include <errno.h>

/* glib */
#include <glib-2.0/glib.h>
#include <gmodule.h>

/* molt */
#include "molt.h"
#include "internal.h"
#include "main.h"
/* rules */
#include "rules-case.h"

/* verbose/debug level */
static level_t     level        = 0;
/* return/error code */
static error_t     err          = ERROR_NONE;
/* list of error messages */
static GSList     *errors       = NULL;
/* list of modules, to be unloaded at the end */
static GSList     *modules      = NULL;
/* list of rules (rule_def_t) */
static GHashTable *rules        = NULL;
/* list of actions to process (not static for use in actions.c ) */
GHashTable        *actions      = NULL;
/* list of taken new names: that is, when an action wants to take a (new) name
 * it will be added to this list (whether it is to-rename or conflict-FS, as the
 * later could be resolved later on) unless there's already one, in which case
 * it'll be set to conflict.
 * We don't keep track of all actions wanting to take that new name, but only
 * whether or not there's (already) one, to detect conflicts. 
 * (not static for use in actions.c ) */
GHashTable        *new_names    = NULL;
/* number of conflicts (standard & FS) (not static for use in actions.c ) */
gint               nb_conflicts = 0;
/* number of actions requiring two-steps renaming jobs (not static for use in actions.c ) */
gint               nb_two_steps = 0;
/* current pathname */
static gchar      *curdir       = NULL;

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
        g_set_error (_error, MOLT_ERROR, 1, "stdin can only be taken once\n");
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

static void
free_action (action_t *action)
{
    debug (LEVEL_VERBOSE, "free-ing action for %s\n", action->file);
    g_free (action->path);
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
set_action_path_file (action_t *action, gchar *file)
{
    gchar *path;
    gchar *s, *p;
    
    debug (LEVEL_VERBOSE, "setting path/file for %s\n", file);
    
    if (file[0] == '/')
    {
        action->path = g_malloc (strlen (file) + 1);
        sprintf (action->path, "%s", file);
        
    }
    else
    {
        action->path = g_malloc (strlen (curdir) + strlen (file) + 2);
        sprintf (action->path, "%s/%s", curdir, file);
    }
    
    path = action->path + 1;
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
            p = strpchr (action->path, path - 2, '/');
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
    
    s = strrchr (action->path, '/');
    *s = '\0';
    action->file = s + 1;
    debug (LEVEL_DEBUG, "path=%s -- file=%s\n", action->path, action->file);
}

static void
split_params (gchar *arg, GPtrArray **params)
{
    gchar *a, *p, *s, *ss;
    gint   i;
    size_t len;
    
    debug (LEVEL_DEBUG, "splitting params: %s\n", arg);
    
    *params = g_ptr_array_new ();
    a = p = arg;
    while ((s = strchr (a, '/')))
    {
        /* make sure it's not escaped */
        for (i = 1, ss = s - 1; ss > a && *ss == '\\'; --ss, ++i)
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
        a = s + 1;
    }
    /* add the last param */
    debug (LEVEL_DEBUG, "param: %s\n", p);
    g_ptr_array_add (*params, (gpointer) p);
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
    
    if (modules)
    {
        destroy_fn destroy;
        
        debug (LEVEL_DEBUG, "closing modules\n");
        for (l = modules; l; l = l->next)
        {
            GModule *module = l->data;
            
            debug (LEVEL_VERBOSE, "getting symbol destroy\n");
            if (G_UNLIKELY (!g_module_symbol (module, "destroy", (gpointer *) &destroy)))
            {
                debug (LEVEL_DEBUG, "symbol destroy not found: %s\n",
                       g_module_error ());
            }
            else
            {
                debug (LEVEL_VERBOSE, "destroy found: %p\n", destroy);
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
            
            debug (LEVEL_VERBOSE, "closing module %p\n", module);
            if (G_UNLIKELY (!g_module_close (module)))
            {
                debug (LEVEL_DEBUG, "unable to close module: %s", g_module_error());
            }
        }
        g_slist_free (modules);
    }
    
    if (curdir)
    {
        free (curdir);
    }
}

static option_t options[] = {
    { OPT_DEBUG,                "debug" },
    { OPT_CONTINUE_ON_ERROR,    "continue-on-error" },
    { OPT_DRY_RUN,              "dry-run" },
    { OPT_EXCLUDE_DIRS,         "exclude-directories" },
    { OPT_EXCLUDE_FILES,        "exclude-files" },
    { OPT_EXCLUDE_SYMLINKS,     "exclude-symlinks" },
    { OPT_OUTPUT,               "output" },
    { OPT_ONLY_RULES,           "only-rules" },
};
static gint nb_options = sizeof (options) / sizeof (options[0]);

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
                fprintf (stdout, "%s -> %s\n", action->file,
                         name);
            }
            break;
        case OUTPUT_BOTH_NAMES:
            fprintf (stdout, "%s\n", action->file);
            /* fall through */
        case OUTPUT_NEW_NAMES:
            fprintf (stdout, "%s\n", name);
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

static gint
do_rename (action_t *action, const gchar *old_name, const gchar *new_name)
{
    gchar *old, *new;
    size_t len_path = strlen (action->path) + 1; /* 1: the '/' */
    gint state;
    
    old = g_malloc (len_path + strlen (old_name) + 1);
    sprintf (old, "%s/%s", action->path, old_name);
    
    new = g_malloc (len_path + strlen (new_name) + 1);
    sprintf (new, "%s/%s", action->path, new_name);
    
    debug (LEVEL_DEBUG, "renaming %s to %s\n", old, new);
    if (G_UNLIKELY (0 != (state = rename (old, new))))
    {
        action_error ("%s: failed to rename to %s: %s\n",
                      action->file, new_name, strerror (errno));
    }
    
    g_free (old);
    g_free (new);
    
    return state;
}

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

int
main (int argc, char **argv)
{
    GError        *local_err = NULL;
    gint           argi  = 1;
    rule_def_t    *rule;
    
    GDir          *dir;
    const gchar   *file;
    GModule       *module;
    init_fn        init;
    
    GSList        *commands = NULL;
    command_t     *command;
    GPtrArray     *ptr_arr;
    guint          i;
    
    gchar         *option;
    GFileTest      test_types = G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_DIR
                                | G_FILE_TEST_IS_SYMLINK;
    gboolean       continue_on_error = FALSE;
    gboolean       dry_run           = FALSE;
    output_t       output            = OUTPUT_STANDARD;
    gboolean       only_rules        = FALSE;
    
    GSList        *actions_list      = NULL;
    action_t      *action;
    guint          cur;
    GSList        *l;
    gchar         *new_name;
    
    /* try to get debug option now so it applies to loading rules as well.
     * Note: only works if the first option is -d[d] (--debug not supported) */
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'd')
    {
        ++level;
        if (argv[1][2] == 'd')
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
    rule->param = PARAM_NONE;
    rule->help = "Convert to lowercase";
    rule->init = NULL;
    rule->run = rule_to_lower;
    rule->destroy = NULL;
    add_rule (rule);
    
    rule->name = "upper";
    rule->param = PARAM_NONE;
    rule->help = "Convert to uppercase";
    rule->init = NULL;
    rule->run = rule_to_upper;
    rule->destroy = NULL;
    add_rule (rule);
    
    rule->name = "camel";
    rule->param = PARAM_NONE;
    rule->help = "Convert to Camel Case";
    rule->init = NULL;
    rule->run = rule_camel;
    rule->destroy = NULL;
    add_rule (rule);
    
    rule->name = "sr";
    rule->param = PARAM_SPLIT;
    rule->help = "Search & replace";
    rule->init = rule_sr_init;
    rule->run = (rule_run_fn) rule_sr;
    rule->destroy = rule_sr_destroy;
    add_rule (rule);
    
    rule->name = "list";
    rule->param = PARAM_NONE;
    rule->help = "List of new names";
    rule->init = rule_list_init;
    rule->run = rule_list;
    rule->destroy = NULL;
    add_rule (rule);
    
    rule->name = "regex";
    rule->param = PARAM_SPLIT;
    rule->help = "Regular expression";
    rule->init = rule_regex_init;
    rule->run = rule_regex;
    rule->destroy = rule_regex_destroy;
    add_rule (rule);
    
    g_free (rule);
    
    debug (LEVEL_DEBUG, "loading modules from %s\n", MODULES_PATH);
    if (!(dir = g_dir_open (MODULES_PATH, 0, &local_err)))
    {
        debug (LEVEL_DEBUG, "cannot load modules: unable to open %s: %s\n",
               MODULES_PATH, local_err->message);
        g_clear_error (&local_err);
    }
    else
    {
        while ((file = g_dir_read_name (dir)))
        {
            debug (LEVEL_VERBOSE, "opening module: %s\n", file);
            module = g_module_open (file, G_MODULE_BIND_LAZY);
            if (G_UNLIKELY (!module))
            {
                debug (LEVEL_DEBUG, "cannot open module %s: %s\n",
                       file, g_module_error ());
                continue;
            }
            
            debug (LEVEL_VERBOSE, "getting symbol init\n");
            if (G_UNLIKELY (!g_module_symbol (module, "init", (gpointer *) &init)))
            {
                debug (LEVEL_DEBUG, "symbol init not found in %s: %s\n",
                       file, g_module_error ());
                if (G_UNLIKELY (!g_module_close (module)))
                {
                    debug (LEVEL_DEBUG, "unable to close module: %s\n",
                           g_module_error ());
                }
                continue;
            }
            
            debug (LEVEL_VERBOSE, "init found: %p\n", init);
            if (G_UNLIKELY (init == NULL))
            {
                debug (LEVEL_DEBUG, "symbol init is NULL in %s: %s\n",
                       file, g_module_error ());
                if (G_UNLIKELY (!g_module_close (module)))
                {
                    debug (LEVEL_DEBUG, "unable to close module: %s\n",
                           g_module_error ());
                }
                continue;
            }
            
            /* store ref to this module */
            debug (LEVEL_DEBUG, "adding module %s\n", file);
            modules = g_slist_prepend (modules, module);
            
            debug (LEVEL_VERBOSE, "call module's init function\n");
            init ();
        }
        debug (LEVEL_DEBUG, "closing folder\n");
        g_dir_close (dir);
    }
    
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
                case OPT_OUTPUT:
                    ++argi;
                    if (strcmp (argv[argi], "new-names") == 0)
                    {
                        output = OUTPUT_NEW_NAMES;
                    }
                    else if (strcmp (argv[argi], "both-names") == 0)
                    {
                        output = OUTPUT_BOTH_NAMES;
                    }
                    else
                    {
                        error (ERROR_SYNTAX, "Invalid output mode: %s\n",
                               argv[argi]);
                    }
                    debug (LEVEL_DEBUG, "output=%d\n", output);
                    ++argi;
                    break;
                case OPT_ONLY_RULES:
                    only_rules = TRUE;
                    /* only-rules implies dry-run */
                    debug (LEVEL_DEBUG, "implied option --dry-run\n");
                    dry_run = TRUE;
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
                    split_params (argv[++argi], &ptr_arr);
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
    
    debug (LEVEL_DEBUG, "process file names, i=%d\n", argi);
    for (cur = 0; argi < argc; ++argi)
    {
        
        if (!g_file_test (argv[argi], G_FILE_TEST_EXISTS))
        {
            error (ERROR_FILE, "file does not exist: %s\n", argv[argi]);
            continue;
        }
        if (test_types == (G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_DIR
                           | G_FILE_TEST_IS_SYMLINK))
        {
            debug (LEVEL_DEBUG, "process: %s\n", argv[argi]);
        }
        else if (test_types & G_FILE_TEST_IS_REGULAR
            && g_file_test (argv[argi], G_FILE_TEST_IS_REGULAR))
        {
            debug (LEVEL_DEBUG, "process file: %s\n", argv[argi]);
        }
        else if (test_types & G_FILE_TEST_IS_DIR
            && g_file_test (argv[argi], G_FILE_TEST_IS_DIR))
        {
            debug (LEVEL_DEBUG, "process dir: %s\n", argv[argi]);
        }
        else if (test_types & G_FILE_TEST_IS_SYMLINK
            && g_file_test (argv[argi], G_FILE_TEST_IS_SYMLINK))
        {
            debug (LEVEL_DEBUG, "process symlink: %s\n", argv[argi]);
        }
        else
        {
            debug (LEVEL_DEBUG, "ignore: %s\n", argv[argi]);
            continue;
        }
        /* create new action */
        action = g_slice_new0 (action_t);
        action->cur = ++cur;
        set_action_path_file (action, argv[argi]);
        /* put in the new name a copy of the current one. this will be free-d
         * and updated after each rule that does provide a new name */
        action->new_name = g_strdup (action->file);
        
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
        /* check whether we actually have a new name or not */
        if (g_strcmp0 (action->new_name, action->file) != 0)
        {
            /* check validity of new name */
            if (strlen (action->new_name) == 0 || strchr (action->new_name, '/'))
            {
                error (ERROR_INVALID_NAME, "%s: invalid new name: %s\n",
                       action->file, action->new_name);
                g_free (action->new_name);
                action->new_name = NULL;
            }
            else
            {
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
        actions_list = g_slist_append (actions_list, (gpointer) action);
    }
    free_commands (commands);
    
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
                        state = do_rename (action, action->file, name);
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
                state = do_rename (action, action->tmp_name, name);
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
