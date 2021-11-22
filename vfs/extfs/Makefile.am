extfsdir = $(pkgdatadir)/extfs

# Files to install and distribute other than extfs scripts
EXTFS_MISC  = README extfs.ini sfs.ini

# Scripts hat don't need adaptation to the local system
EXTFS_CONST = bpp rpm trpm u7z uhfs

# Scripts that need adaptation to the local system - source files
EXTFS_IN    = 			\
	a.in			\
	apt.in			\
	audio.in		\
	deb.in			\
	deba.in			\
	debd.in			\
	dpkg.in			\
	iso9660.in		\
	fat.in			\
	findfs.in		\
	hp48.in			\
	img4.in			\
	lslR.in			\
	mailfs.in		\
	patchfs.in		\
	rpms.in			\
	ualz.in			\
	uar.in			\
	uarj.in			\
	uha.in			\
	ulha.in			\
	urar.in			\
	uxar.in			\
	uzip.in			\
	uzoo.in

# Scripts that need adaptation to the local system - files to install
EXTFS_OUT = 			\
	a			\
	apt			\
	audio			\
	deb			\
	deba			\
	debd			\
	dpkg			\
	iso9660			\
	fat			\
	findfs			\
	hp48			\
	img4			\
	lslR			\
	mailfs			\
	patchfs			\
	rpms			\
	ualz			\
	uar			\
	uarj			\
	uha			\
	ulha			\
	urar			\
	uxar			\
	uzip			\
	uzoo

if USE_VFS
extfs_DATA = $(EXTFS_MISC)
extfs_SCRIPTS = $(EXTFS_CONST) $(EXTFS_OUT)
endif

EXTRA_DIST = $(EXTFS_MISC) $(EXTFS_CONST)

