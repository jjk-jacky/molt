
#ifndef MAIN_H
#define	MAIN_H

#ifdef	__cplusplus
extern "C" {
#endif

#define MODULES_PATH			"./modules/"

#define OPT_DEBUG                   'd'
#define OPT_CONTINUE_ON_ERROR       'C'
#define OPT_DRY_RUN                 'n'
#define OPT_EXCLUDE_FILES           'F'
#define OPT_EXCLUDE_DIRS            'D'
#define OPT_EXCLUDE_SYMLINKS        'S'
#define OPT_OUTPUT_BOTH             'B'
#define OPT_OUTPUT_NEW              'N'
#define OPT_ONLY_RULES              'R'
#define OPT_PROCESS_FULLNAME        'P'
#define OPT_OUTPUT_FULLNAME         'O'
#define OPT_ALLOW_PATH              'p'
#define OPT_FROM_STDIN              'i'

typedef struct {
	gchar        opt_short;
	const gchar *opt_long;
} option_t;

/* command: rule to run w/ its parameter(s) */
typedef struct {
    rule_def_t *rule;
    gpointer    data;
} command_t;

/* different type of output */
typedef enum {
	OUTPUT_STANDARD = 0,	/* regular stuff */
	OUTPUT_BOTH_NAMES,		/* list of old & new names */
	OUTPUT_NEW_NAMES,		/* list of (new) names */
} output_t;

static void free_memory (void);


#ifdef	__cplusplus
}
#endif

#endif	/* MAIN_H */
