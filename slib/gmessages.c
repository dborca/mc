#include <config.h>
#include <stdio.h>
#include "glib.h"

gsize
g_printf_string_upper_bound (const gchar *format,
			     va_list      args)
{
  gchar c;
  return vsnprintf (&c, 1, format, args) + 1;
}

void
g_return_if_fail_warning (const char *log_domain,
			  const char *pretty_function,
			  const char *expression)
{
  fprintf (stderr,
	 "%s: assertion `%s' failed",
	 pretty_function,
	 expression);
}
