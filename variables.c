
/* glib */
#include <glib-2.0/glib.h>

/* molt */
#include "internal.h"
#include "molt.h"
#include "variables.h"

gchar *
var_ask_nb (const gchar *name, const gchar *file, gpointer params, GError **error)
{
    static const gchar *last_file = NULL;
    static guint        cnt = 0;
    
    g_return_val_if_fail (name[0] == 'N' && name[1] == 'B' && name[2] == '\0',
                          NULL);
    
    /* if it's a new file, we increment the counter */
    if (last_file != file)
    {
        last_file = file;
        ++cnt;
    }
    
    return g_strdup_printf ("%u", cnt);
}
