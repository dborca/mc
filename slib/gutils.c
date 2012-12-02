#include <config.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include "glib.h"

#ifdef	MAXPATHLEN
#define	G_PATH_LENGTH	MAXPATHLEN
#elif	defined (PATH_MAX)
#define	G_PATH_LENGTH	PATH_MAX
#elif   defined (_PC_PATH_MAX)
#define	G_PATH_LENGTH	sysconf(_PC_PATH_MAX)
#else	
#define G_PATH_LENGTH   2048
#endif

gchar*
g_get_current_dir (void)
{
#ifdef _WIN32

  gchar *dir = NULL;
  wchar_t dummy[2], *wdir;
  int len;

  len = GetCurrentDirectoryW (2, dummy);
  wdir = g_new (wchar_t, len);

  if (GetCurrentDirectoryW (len, wdir) == len - 1)
    dir = g_utf16_to_utf8 (wdir, -1, NULL, NULL, NULL);
  
  g_free (wdir);

  if (dir == NULL)
    dir = g_strdup ("\\");

  return dir;

#else

  gchar *buffer = NULL;
  gchar *dir = NULL;
  static gulong max_len = 0;

  if (max_len == 0) 
    max_len = (G_PATH_LENGTH == -1) ? 2048 : G_PATH_LENGTH;
  
  /* We don't use getcwd(3) on SUNOS, because, it does a popen("pwd")
   * and, if that wasn't bad enough, hangs in doing so.
   */
#if	(defined (sun) && !defined (__SVR4)) || !defined(HAVE_GETCWD)
  buffer = g_new (gchar, max_len + 1);
  *buffer = 0;
  dir = getwd (buffer);
#else	/* !sun || !HAVE_GETCWD */
  while (max_len < G_MAXULONG / 2)
    {
      g_free (buffer);
      buffer = g_new (gchar, max_len + 1);
      *buffer = 0;
      dir = getcwd (buffer, max_len);

      if (dir || errno != ERANGE)
	break;

      max_len *= 2;
    }
#endif	/* !sun || !HAVE_GETCWD */
  
  if (!dir || !*buffer)
    {
      /* hm, should we g_error() out here?
       * this can happen if e.g. "./" has mode \0000
       */
      buffer[0] = G_DIR_SEPARATOR;
      buffer[1] = 0;
    }

  dir = g_strdup (buffer);
  g_free (buffer);
  
  return dir;
#endif /* !Win32 */
}
