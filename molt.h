
#ifndef MOLT_H
#define	MOLT_H

#ifdef	__cplusplus
extern "C" {
#endif


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
    param_t         param;
    const gchar    *help;
    rule_init_fn    init;
    rule_run_fn     run;
    rule_destroy_fn destroy;
    gboolean        parse_variables;
} rule_def_t;

/* function called by molt so rules can be added */
typedef void (*init_fn) (void);

/* function called by molt when terminating to e.g. clean up memory */
typedef void (*destroy_fn) (void);

typedef enum {
    VAR_TYPE_GLOBAL = 0,
    VAR_TYPE_PER_FILE
} var_type_t;

/* function called by molt to ask value of a variable */
typedef gchar * (*var_ask_fn) (const gchar *name,
                               const gchar *file,
                               GPtrArray   *params,
                               GError **error);

typedef struct {
    const gchar *name;
    var_type_t   type;
    param_t      param;
    var_ask_fn   ask;
} var_def_t;

/* function called by molt so variables can be added */
typedef void (*init_vars_fn) (void);

typedef enum {
	LEVEL_DEBUG = 1,
	LEVEL_VERBOSE
} level_t;

void debug (level_t lvl, const gchar *fmt, ...);

gboolean get_stdin (gpointer *stream, GError **error);

gboolean add_rule (rule_def_t *rule);

gboolean add_var (var_def_t *variable);

gboolean add_var_value (const gchar *name, gchar *params, gchar *value);

#ifdef	__cplusplus
}
#endif

#endif	/* MOLT_H */

