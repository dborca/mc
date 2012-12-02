#include <dlfcn.h>
#include <string.h>

#define G_MODULE_SUFFIX "so"

gboolean
g_module_supported(void)
{
    return TRUE;
}

GModule *
g_module_open(const gchar *file_name, GModuleFlags flags)
{
    return dlopen(file_name,
		  ((flags & G_MODULE_BIND_LOCAL) ? 0 : RTLD_GLOBAL)
		  |
		  ((flags & G_MODULE_BIND_LAZY) ? RTLD_LAZY : RTLD_NOW)
		 );
}

gboolean
g_module_close(GModule *module)
{
    return dlclose(module) == 0;
}

gboolean
g_module_symbol(GModule *module, const gchar *symbol_name, gpointer *symbol)
{
    *symbol = dlsym(module, symbol_name);
    return (*symbol != NULL);
}

gchar *
g_module_build_path(const gchar *directory, const gchar *module_name)
{
  if (directory && *directory) {
    if (strncmp (module_name, "lib", 3) == 0)
      return g_strconcat (directory, "/", module_name, NULL);
    else
      return g_strconcat (directory, "/lib", module_name, "." G_MODULE_SUFFIX, NULL);
  } else if (strncmp (module_name, "lib", 3) == 0)
    return g_strdup (module_name);
  else
    return g_strconcat ("lib", module_name, "." G_MODULE_SUFFIX, NULL);
}
