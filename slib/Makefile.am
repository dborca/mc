AM_CFLAGS = $(GLIB_CFLAGS)

if INCLUDED_GLIB
noinst_LIBRARIES = libslib.a
else
noinst_LIBRARIES =
endif

SLIB_INCLUDES = \
	array.h \
	hash.h \
	glib.h \
	gmodule.h

libslib_a_SOURCES = \
	garray.c \
	glist.c \
	gmem.c \
	gmessages.c \
	gprintf.c \
	gstrfuncs.c \
	gstring.c \
	gutils.c \
	gmodule.c \
	glib.c \
	$(SLIB_INCLUDES)

EXTRA_DIST = README
