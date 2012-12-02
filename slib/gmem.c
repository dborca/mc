#include <config.h>
#include <stdlib.h>
#include "glib.h"

gpointer
g_malloc (gsize n_bytes)
{
  if (G_LIKELY (n_bytes))
    {
      gpointer mem;

      mem = malloc (n_bytes);
      if (mem)
	return mem;

      assert(mem);
    }

  return NULL;
}

gpointer
g_malloc0 (gsize n_bytes)
{
  if (G_LIKELY (n_bytes))
    {
      gpointer mem;

      mem = calloc (1, n_bytes);
      if (mem)
	return mem;

      assert(mem);
    }

  return NULL;
}

gpointer
g_realloc (gpointer mem,
	   gsize    n_bytes)
{
  if (G_LIKELY (n_bytes))
    {
      mem = realloc (mem, n_bytes);
      if (mem)
	return mem;

      assert(mem);
    }

  if (mem)
    free (mem);

  return NULL;
}

void
g_free (gpointer mem)
{
  if (G_LIKELY (mem))
    free (mem);
}

gpointer
g_try_malloc (gsize n_bytes)
{
  if (G_LIKELY (n_bytes))
    return malloc (n_bytes);
  else
    return NULL;
}

gpointer
g_try_realloc (gpointer mem,
	       gsize    n_bytes)
{
  if (G_LIKELY (n_bytes))
    return realloc (mem, n_bytes);

  if (mem)
    free (mem);

  return NULL;
}
