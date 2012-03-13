
/* C */
#include <stdlib.h> /* atoi() */

/* glib */
#include <glib-2.0/glib.h>

/* molt */
#include "molt.h"
#include "variables.h"

gchar *
var_get_value_nb (const gchar *file, GPtrArray *params, GError **error)
{
    static const gchar *last_file   = NULL;
    static guint        cnt         = 0;
    guint               digits      = 0;
    guint               start       = 1;
    guint               incr        = 1;
    
    if (params)
    {
        if (params->len >= 1)
        {
            digits = (guint) atoi (g_ptr_array_index (params, 0));
        }
        if (params->len >= 2)
        {
            /* make sure there is something specified. if not, we keep our default */
            if (((gchar *)(params->pdata[1]))[0] != '\0')
            {
                start = (guint) atoi (g_ptr_array_index (params, 1));
            }
        }
        if (params->len >= 3)
        {
            /* make sure there is something specified. if not, we keep our default */
            if (((gchar *)(params->pdata[2]))[0] != '\0')
            {
                incr = (guint) atoi (g_ptr_array_index (params, 2));
            }
        }
    }
    
    /* is this the first file? */
    if (last_file == NULL)
    {
        last_file = file;
        cnt = start;
    }
    /* if it's a new file, we increment the counter */
    else if (last_file != file)
    {
        last_file = file;
        cnt += incr;
    }
    
    if (digits)
    {
        return g_strdup_printf ("%0*u", digits, cnt);
    }
    else
    {
        return g_strdup_printf ("%u", cnt);
    }
}
