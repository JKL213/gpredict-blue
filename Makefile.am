ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src doc icons pixmaps data po

BUILT_SOURCES = $(top_builddir)/.version
MOSTLYCLEANFILES = $(top_builddir)/.version
$(top_builddir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version

pkgdata_DATA = AUTHORS NEWS README COPYING

.PHONY: run
run:
	@$(top_srcdir)/scripts/run-gpredict-blue.sh

EXTRA_DIST = \
	autogen.sh            \
	git-version-gen       \
	scripts/run-gpredict-blue.sh \
	intltool-extract.in   \
	intltool-merge.in     \
	intltool-update.in    \
	snap/snapcraft.yaml   \
	flatpak/io.github.jkl213.GpredictBlue.yml \
	flatpak/README.md \
	win32/build-config.h  \
	win32/config.mk       \
	win32/gpredict.rc     \
	win32/Makefile        \
	win32/README          \
	win32/resource.h      \
	win32/win32-fetch.c   \
	win32/win32-fetch.h   \
	win32/icons/gpredict-icon.ico
