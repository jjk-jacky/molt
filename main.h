/**
 * molt - Copyright (C) 2012 Olivier Brunel
 *
 * main.h
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

#ifndef MAIN_H
#define	MAIN_H

#ifdef	__cplusplus
extern "C" {
#endif

#define APP_VERSION                 "0.0.1"
#define PLUGINS_PATH                "/usr/lib/molt/"

#define OPT_EXCLUDE_DIRS            'D'
#define OPT_EXCLUDE_FILES           'F'
#define OPT_EXCLUDE_SYMLINKS        'S'
#define OPT_FROM_STDIN              'i'

#define OPT_PROCESS_FULLNAME        'P'
#define OPT_ALLOW_PATH              'p'
#define OPT_MAKE_PARENTS            'm'

#define OPT_OUTPUT_FULLNAME         'O'
#define OPT_OUTPUT_BOTH             'B'
#define OPT_OUTPUT_NEW              'N'
#define OPT_ONLY_RULES              'R'

#define OPT_DRY_RUN                 'n'
#define OPT_CONTINUE_ON_ERROR       'C'

#define OPT_DEBUG                   'd'
#define OPT_HELP                    'h'
#define OPT_VERSION                 'V'

typedef struct {
	gchar        opt_short;
	const gchar *opt_long;
    const gchar *help;
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

struct _plugin_priv_t {
    gchar   *file;
    GModule *module;
};

static void free_memory (void);


#ifdef	__cplusplus
}
#endif

#endif	/* MAIN_H */
