
#ifndef MOLT_H
#define	MOLT_H

#ifdef	__cplusplus
extern "C" {
#endif

/* glib */
#include <glib-2.0/glib.h>

/* Current API version: incremented when on any plugin API changes */
#define MOLT_API_VERSION   1
/* Current ABI version: incremented on binary interface changes, i.e. plugin
 * data types change and plugin needs to be recompiled with new header.
 * Adding data to struct will not increment it, since it wouldn't cause
 * trouble. Removing/re-ordering however requires a bump. */
#define MOLT_ABI_VERSION   1

typedef enum {
	PARAM_NONE = 0,
	PARAM_SPLIT,
	PARAM_NO_SPLIT
} param_t;

/* function called by molt to init a rule (a command really) */
typedef gboolean (*rule_init_fn) (gpointer    *data,
                                  GPtrArray   *params,
                                  GError     **error);

/* function called by molt to run a rule */
typedef gboolean (*rule_run_fn) (gpointer    *data,
                                 const gchar *name,
                                 gchar      **new_name,
                                 GError     **error);

/* function called by molt to destroy/free a rule (command really) */
typedef void (*rule_destroy_fn) (gpointer *data);

/* definition of a rule */
typedef struct {
    const gchar    *name;
    const gchar    *description;
    const gchar    *help;
    param_t         param;
    rule_init_fn    init;
    rule_run_fn     run;
    rule_destroy_fn destroy;
    gboolean        parse_variables;
} rule_def_t;

typedef enum {
    VAR_TYPE_GLOBAL = 0,
    VAR_TYPE_PER_FILE
} var_type_t;

/* function called by molt to ask value of a variable */
typedef gchar * (*var_get_value_fn) (const gchar *file,
                                     GPtrArray   *params,
                                     GError     **error);

typedef struct {
    const gchar     *name;
    const gchar     *description;
    const gchar     *help;
    var_type_t       type;
    param_t          param;
    var_get_value_fn get_value;
} var_def_t;

typedef enum {
	LEVEL_DEBUG = 1,
	LEVEL_VERBOSE
} level_t;

/* info about the plugin */
typedef struct {
    const gchar         *name;
    const gchar         *description;
    const gchar         *version;
    const gchar         *author;
} plugin_info_t;

/* pointers to molt's functions */
typedef struct {
    void     (*debug)         (level_t lvl, const gchar *fmt, ...);
    gboolean (*get_stdin)     (gpointer *stream, GError **error);
    gboolean (*add_rule)      (rule_def_t *rule);
    gboolean (*add_var)       (var_def_t *variable);
    gboolean (*add_var_value) (const gchar *name, gchar *params, gchar *value);
} plugin_functions_t;

/* private structure for molt */
typedef struct _plugin_priv_t plugin_priv_t;

/* info & internals for a plugin */
typedef struct {
    plugin_info_t       *info;
    plugin_functions_t  *functions;
    plugin_priv_t       *priv;
} plugin_t;

/* plugin_check_version: function called by molt to check API/ABI version compatibility */
typedef gint (*check_version_fn) (gint abi_ver);

/* plugin_set_info: function called by molt to get plugin's information */
typedef void (*set_info_fn) (plugin_info_t *info);

/* plugin_init: function called by molt so rules can be added */
typedef void (*init_fn) (void);

/* plugin_init_vars: function called by molt so variables can be added */
typedef void (*init_vars_fn) (void);

/* plugin_destroy: function called by molt when terminating to e.g. clean up memory */
typedef void (*destroy_fn) (void);

#ifndef IS_MOLT

#define PLUGIN_VERSION_CHECK(api_required)      \
    gint plugin_check_version (gint abi_ver)    \
    {                                           \
        if (abi_ver != MOLT_ABI_VERSION)        \
        {                                       \
            return -1;                          \
        }                                       \
        return api_required;                    \
    }

#define PLUGIN_SET_INFO(_name, _description, _version, _author) \
    void plugin_set_info (plugin_info_t *info)                  \
    {                                                           \
        info->name          = _name;                            \
        info->description   = _description;                     \
        info->version       = _version;                         \
        info->author        = _author;                          \
    }

#define molt_debug(level, ...)  \
    molt_plugin->functions->debug (level, __VA_ARGS__)
#define molt_get_stdin(stream, error)   \
    molt_plugin->functions->get_stdin (stream, error)
#define molt_add_rule(rule) \
    molt_plugin->functions->add_rule (rule)
#define molt_add_var(variable)  \
    molt_plugin->functions->add_var (variable)
#define molt_add_var_value(name, params, value) \
    molt_plugin->functions->add_var_value (name, params, value)

#endif  /* IS_MOLT */

#ifdef	__cplusplus
}
#endif

#endif	/* MOLT_H */

