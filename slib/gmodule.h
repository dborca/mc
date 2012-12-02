#ifndef __GMODULE_H__
#define __GMODULE_H__

#include <glib.h>

typedef enum
{
  G_MODULE_BIND_LAZY	= 1 << 0,
  G_MODULE_BIND_LOCAL	= 1 << 1,
  G_MODULE_BIND_MASK	= 0x03
} GModuleFlags;

typedef	struct _GModule			 GModule;

gboolean	g_module_supported	   (void) G_GNUC_CONST;

GModule*              g_module_open          (const gchar  *file_name,
					      GModuleFlags  flags);

gboolean              g_module_close         (GModule      *module);

gboolean              g_module_symbol        (GModule      *module,
					      const gchar  *symbol_name,
					      gpointer     *symbol);

gchar*                g_module_build_path    (const gchar  *directory,
					      const gchar  *module_name);

#endif /* __GMODULE_H__ */
