#include <config.h>
#include "gmodule.h"

#if defined(__linux__)
#include "gmodule-dl.c"
#elif defined(__MACH__)
#include "gmodule-dyld.c"
#else

gboolean
g_module_supported(void)
{
    return FALSE;
}

GModule *
g_module_open(const gchar *file_name, GModuleFlags flags)
{
    return NULL;
}

gboolean
g_module_close(GModule *module)
{
    return FALSE;
}

gboolean
g_module_symbol(GModule *module, const gchar *symbol_name, gpointer *symbol)
{
    *symbol = NULL;
    return FALSE;
}

gchar *
g_module_build_path(const gchar *directory, const gchar *module_name)
{
    return NULL;
}

#endif
