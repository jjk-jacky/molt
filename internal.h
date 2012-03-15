
#ifndef INTERNAL_H
#define	INTERNAL_H

#ifdef	__cplusplus
extern "C" {
#endif

#define MOLT_ERROR          g_quark_from_static_string ("molt error")

typedef enum {
	ST_NONE			= 0,		/* if processed, means no renaming to be done */
	ST_CONFLICT		= (1 << 0), /* conflict with another action */
	ST_CONFLICT_FS	= (1 << 1), /* conflict w/ file system */
	ST_TO_RENAME	= (1 << 2), /* to be renamed */
	ST_TWO_STEPS	= (1 << 3), /* must be done in two-steps */
} state_t;

/* return code is ERROR_NONE on success, else uses the following : */
typedef enum {
	ERROR_NONE              = 0,
	ERROR_SYNTAX            = (1 << 0), /* syntax error */
	ERROR_FILE              = (1 << 1), /* file not found */
	ERROR_RULE_FAILED       = (1 << 2), /* rule failed (e.g. syntax error) */
    ERROR_INVALID_NAME      = (1 << 3), /* invalid new name */
	ERROR_CONFLICT_FS      	= (1 << 4), /* new name already in use */
	ERROR_CONFLICT_RENAME  	= (1 << 5), /* multiple actions want the same name */
	ERROR_RENAME_FAILURE    = (1 << 6)  /* unable to perform rename */
} error_t;

/* action: original filename, new one, etc */
typedef struct {
    guint     cur;
	gchar    *file;
    gchar    *filename;
	gchar    *new_name;
    gchar    *new_filename;
	gchar    *tmp_name;
	state_t   state;
    gchar    *error;
} action_t;

/* main.c */
void debug (level_t lvl, const gchar *fmt, ...);
gboolean get_stdin (gpointer *stream, GError **error);
gboolean add_rule (rule_def_t *rule);
gboolean add_var (var_def_t *variable);
gboolean add_var_value (const gchar *name, gchar *params, gchar *value);

/* actions.c */
void set_to_rename (action_t *action, action_t *action_for);

#ifdef	__cplusplus
}
#endif

#endif	/* INTERNAL_H */

