/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifndef __G_LIB_H__
#define __G_LIB_H__

#include <assert.h>
#include <stdarg.h>

#define GLIB_MAJOR_VERSION 0

/*** gmacros.h ***************************************************************/

#if    __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 96)
#define G_GNUC_MALLOC    			\
  __attribute__((__malloc__))
#else
#define G_GNUC_MALLOC
#endif

#if     __GNUC__ >= 4
#define G_GNUC_NULL_TERMINATED __attribute__((__sentinel__))
#else
#define G_GNUC_NULL_TERMINATED
#endif

#if     (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3)
#define G_GNUC_ALLOC_SIZE(x) __attribute__((__alloc_size__(x)))
#else
#define G_GNUC_ALLOC_SIZE(x)
#endif

#if     __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4)
#define G_GNUC_PRINTF( format_idx, arg_idx )    \
  __attribute__((__format__ (__printf__, format_idx, arg_idx)))
#define G_GNUC_CONST                            \
  __attribute__((__const__))
#else   /* !__GNUC__ */
#define G_GNUC_PRINTF( format_idx, arg_idx )
#define G_GNUC_CONST
#endif  /* !__GNUC__ */

#if    __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)
#define G_GNUC_WARN_UNUSED_RESULT 		\
  __attribute__((warn_unused_result))
#else
#define G_GNUC_WARN_UNUSED_RESULT
#endif /* __GNUC__ */

#define G_STRINGIFY(macro_or_string)	G_STRINGIFY_ARG (macro_or_string)
#define	G_STRINGIFY_ARG(contents)	#contents

#if defined(__GNUC__) && (__GNUC__ < 3) && !defined(__cplusplus)
#  define G_STRLOC	__FILE__ ":" G_STRINGIFY (__LINE__) ":" __PRETTY_FUNCTION__ "()"
#else
#  define G_STRLOC	__FILE__ ":" G_STRINGIFY (__LINE__)
#endif

#ifndef	FALSE
#define	FALSE	(0)
#endif

#ifndef	TRUE
#define	TRUE	(!FALSE)
#endif

#if !(defined (G_STMT_START) && defined (G_STMT_END))
#  define G_STMT_START  do
#  define G_STMT_END    while (0)
#endif

#if defined(__GNUC__) && (__GNUC__ > 2) && defined(__OPTIMIZE__)
#define G_LIKELY(expr) (__builtin_expect (!!(expr), 1))
#define G_UNLIKELY(expr) (__builtin_expect (!!(expr), 0))
#else
#define G_LIKELY(expr) (expr)
#define G_UNLIKELY(expr) (expr)
#endif

/*** glibconfig.h ************************************************************/

#define G_MAXULONG	ULONG_MAX

typedef signed char gint8;
typedef unsigned char guint8;
typedef unsigned short guint16;
typedef unsigned int guint32;

/* XXX may be wrong on 64-bit */
typedef unsigned int gsize;

#define G_VA_COPY	va_copy

/*** gtypes.h ****************************************************************/

typedef char   gchar;
typedef int    gint;
typedef gint   gboolean;

typedef unsigned char   guchar;
/* XXX may be wrong? */
typedef unsigned long   gulong;
typedef unsigned int    guint;


typedef void* gpointer;
typedef const void *gconstpointer;

typedef gint            (*GCompareFunc)         (gconstpointer  a,
                                                 gconstpointer  b);
typedef gint            (*GCompareDataFunc)     (gconstpointer  a,
                                                 gconstpointer  b,
						 gpointer       user_data);
typedef void            (*GDestroyNotify)       (gpointer       data);
typedef void            (*GFunc)                (gpointer       data,
                                                 gpointer       user_data);

#define GUINT16_SWAP_LE_BE_CONSTANT(val)	((guint16) ( \
    (guint16) ((guint16) (val) >> 8) |	\
    (guint16) ((guint16) (val) << 8)))

/*** gmem.h ******************************************************************/

gpointer g_malloc         (gsize	 n_bytes) G_GNUC_MALLOC G_GNUC_ALLOC_SIZE(1);
gpointer g_malloc0        (gsize	 n_bytes) G_GNUC_MALLOC G_GNUC_ALLOC_SIZE(1);
gpointer g_realloc        (gpointer	 mem,
			   gsize	 n_bytes) G_GNUC_WARN_UNUSED_RESULT;
void	 g_free	          (gpointer	 mem);
gpointer g_try_malloc     (gsize	 n_bytes) G_GNUC_MALLOC G_GNUC_ALLOC_SIZE(1);
gpointer g_try_realloc    (gpointer	 mem,
			   gsize	 n_bytes) G_GNUC_WARN_UNUSED_RESULT;

#define g_new(struct_type, n_structs)		\
    ((struct_type *) g_malloc (((gsize) sizeof (struct_type)) * ((gsize) (n_structs))))
#define g_new0(struct_type, n_structs)		\
    ((struct_type *) g_malloc0 (((gsize) sizeof (struct_type)) * ((gsize) (n_structs))))

/*** glist.h *****************************************************************/

typedef struct _GList GList;

struct _GList
{
  gpointer data;
  GList *next;
  GList *prev;
};

void     g_list_free                    (GList            *list);
void     g_list_free_1                  (GList            *list);
GList*   g_list_append                  (GList            *list,
					 gpointer          data) G_GNUC_WARN_UNUSED_RESULT;
GList*   g_list_prepend                 (GList            *list,
					 gpointer          data) G_GNUC_WARN_UNUSED_RESULT;
GList*   g_list_remove                  (GList            *list,
					 gconstpointer     data) G_GNUC_WARN_UNUSED_RESULT;
GList*   g_list_remove_link             (GList            *list,
					 GList            *llink) G_GNUC_WARN_UNUSED_RESULT;
GList*   g_list_reverse                 (GList            *list) G_GNUC_WARN_UNUSED_RESULT;
GList*   g_list_find_custom             (GList            *list,
					 gconstpointer     data,
					 GCompareFunc      func);
GList*   g_list_last                    (GList            *list);
GList*   g_list_first                   (GList            *list);
void     g_list_foreach                 (GList            *list,
					 GFunc             func,
					 gpointer          user_data);


#define g_list_previous(list)	        ((list) ? (((GList *)(list))->prev) : NULL)
#define g_list_next(list)	        ((list) ? (((GList *)(list))->next) : NULL)

/*** gutils.h ****************************************************************/

#if defined(__linux__) || defined(__MACH__)
#define G_DIR_SEPARATOR '/'
#endif

gint                  g_snprintf           (gchar       *string,
					    gulong       n,
					    gchar const *format,
					    ...) G_GNUC_PRINTF (3, 4);
gint                  g_vsnprintf          (gchar       *string,
					    gulong       n,
					    gchar const *format,
					    va_list      args);

gchar*                g_get_current_dir    (void);

/*** gstrfuncs.h *************************************************************/

typedef enum {
  G_ASCII_ALNUM  = 1 << 0,
  G_ASCII_ALPHA  = 1 << 1,
  G_ASCII_CNTRL  = 1 << 2,
  G_ASCII_DIGIT  = 1 << 3,
  G_ASCII_GRAPH  = 1 << 4,
  G_ASCII_LOWER  = 1 << 5,
  G_ASCII_PRINT  = 1 << 6,
  G_ASCII_PUNCT  = 1 << 7,
  G_ASCII_SPACE  = 1 << 8,
  G_ASCII_UPPER  = 1 << 9,
  G_ASCII_XDIGIT = 1 << 10
} GAsciiType;

extern const guint16 * const g_ascii_table;

#define g_ascii_isspace(c) \
  ((g_ascii_table[(guchar) (c)] & G_ASCII_SPACE) != 0)

gchar*	              g_strreverse     (gchar	     *string);
gsize	              g_strlcpy	       (gchar	     *dest,
					const gchar  *src,
					gsize         dest_size);

gchar*                g_strstrip       (gchar        *string);

gint	              g_strcasecmp     (const gchar *s1,
					const gchar *s2);
gint	              g_strncasecmp    (const gchar *s1,
					const gchar *s2,
					guint        n);
gchar*	              g_strup	       (gchar	     *string);

gchar*	              g_strdup	       (const gchar *str) G_GNUC_MALLOC;
gchar*	              g_strdup_printf  (const gchar *format,
					...) G_GNUC_PRINTF (1, 2) G_GNUC_MALLOC;
gchar*	              g_strdup_vprintf (const gchar *format,
					va_list      args) G_GNUC_MALLOC;
gchar*	              g_strndup	       (const gchar *str,
					gsize        n) G_GNUC_MALLOC;  
gchar*	              g_strnfill       (gsize        length,  
					gchar        fill_char) G_GNUC_MALLOC;
gchar*	              g_strconcat      (const gchar *string1,
					...) G_GNUC_MALLOC G_GNUC_NULL_TERMINATED;

gchar*                g_stpcpy         (gchar        *dest,
                                        const char   *src);

/*** gprintf.h ***************************************************************/

gint                  g_vasprintf (gchar      **string,
				   gchar const *format,
				   va_list      args);

/*** gmessages.h *************************************************************/

gsize	g_printf_string_upper_bound (const gchar* format,
				     va_list	  args);

void g_return_if_fail_warning (const char *log_domain,
			       const char *pretty_function,
			       const char *expression);

#ifndef G_LOG_DOMAIN
#define G_LOG_DOMAIN    ((gchar*) 0)
#endif  /* G_LOG_DOMAIN */

#define g_return_if_fail(expr)		G_STMT_START{			\
     if G_LIKELY(expr) { } else       					\
       {								\
	 g_return_if_fail_warning (G_LOG_DOMAIN,			\
		                   __PRETTY_FUNCTION__,		        \
		                   #expr);				\
	 return;							\
       };				}G_STMT_END

#define g_return_val_if_fail(expr,val)	G_STMT_START{			\
     if G_LIKELY(expr) { } else						\
       {								\
	 g_return_if_fail_warning (G_LOG_DOMAIN,			\
		                   __PRETTY_FUNCTION__,		        \
		                   #expr);				\
	 return (val);							\
       };				}G_STMT_END

/*****************************************************************************/

#undef g_assert
#undef g_assert_not_reached
#define g_assert assert
#define g_assert_not_reached() assert(!"code should not be reached")

#define g_strerror strerror
#define g_memmove memmove

#define g_mem_is_system_malloc() !0

#define HAVE_GOOD_PRINTF 1	/* XXX ok, this may suck */

/*****************************************************************************/

#include "array.h"
#include "hash.h"

/*****************************************************************************/

#define GPtrArray ARRAY

#define    g_ptr_array_index(array,index_) ((void **)(array)->data)[index_]

ARRAY *g_ptr_array_new(void);
void g_ptr_array_free(ARRAY *a, int free_seg);
int g_ptr_array_add(ARRAY *a, const void *p);
void *g_ptr_array_remove_index(ARRAY *a, unsigned int i);

#define GArray ARRAY

#define g_array_index(a,t,i)      (((t*) (void *) (a)->data) [(i)])
#define g_array_append_val(a, p) g_array_append_val_(a, &p)

ARRAY *g_array_new(int z, int c, int eltsize);
void g_array_free(ARRAY *a, int free_seg);
int g_array_append_val_(ARRAY *a, const void *p);

/*****************************************************************************/

#define GHashTable hash_table

#define g_hash_table_new(str_hash, str_equ) hash_table_new(0, str_hash, str_equ, NULL, NULL)
#define g_hash_table_destroy hash_table_destroy
#define g_hash_table_insert hash_table_insert
#define g_hash_table_lookup hash_table_lookup
#define g_hash_table_foreach hash_table_foreach

#define g_str_equal (compare_func)&strcmp /* XXX g_str_equal != strcmp, but we need it that way for g_hash_table_new() */
guint g_str_hash (gconstpointer v);

/*****************************************************************************/

#define GSList GList

#define g_slist_free		g_list_free
#define g_slist_free_1		g_list_free_1
#define g_slist_append		g_list_append
#define g_slist_prepend		g_list_prepend
#define g_slist_remove		g_list_remove
#define g_slist_remove_link	g_list_remove_link
#define g_slist_reverse		g_list_reverse
#define g_slist_find_custom	g_list_find_custom
#define g_slist_last		g_list_last
#define g_slist_first		g_list_first
#define g_slist_foreach		g_list_foreach

#define g_slist_previous	g_list_previous
#define g_slist_next		g_list_next

#endif /* __G_LIB_H__ */
