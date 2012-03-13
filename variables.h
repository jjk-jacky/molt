
#ifndef VARIABLES_H
#define	VARIABLES_H

#ifdef	__cplusplus
extern "C" {
#endif

gchar *
var_get_value_nb (const gchar *file, GPtrArray *params, GError **error);


#ifdef	__cplusplus
}
#endif

#endif	/* VARIABLES_H */

