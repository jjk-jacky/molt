
/* glib */
#include <glib-2.0/glib.h>

/* molt */
#include "internal.h"
#include "molt.h"
#include "variables.h"

gchar *
var_ask_nb (const gchar *name, const gchar *file, GPtrArray *params, GError **error)
{
    static const gchar *last_file = NULL;
    static guint        cnt = 0;
    
    /* if it's a new file, we increment the counter */
    if (last_file != file)
    {
        last_file = file;
        ++cnt;
    }
    
    return g_strdup_printf ("%u", cnt);
}
