ACLOCAL_AMFLAGS = -I m4

insmod_SOURCES = insmod.c util.c logging.c util.h logging.h testing.h
lsmod_SOURCES = lsmod.c util.c logging.c util.h logging.h testing.h
modprobe_SOURCES = modprobe.c util.c logging.c index.c zlibsupport.c config_filter.c util.h logging.h index.h testing.h zlibsupport.h config_filter.h
rmmod_SOURCES = rmmod.c util.c logging.c util.h logging.h testing.h
depmod_SOURCES = depmod.c util.c logging.c index.c moduleops.c tables.c zlibsupport.c config_filter.c util.h depmod.h logging.h index.h moduleops.h tables.h list.h testing.h  zlibsupport.h config_filter.h
modinfo_SOURCES = modinfo.c util.c logging.c zlibsupport.c util.h testing.h zlibsupport.h
modindex_SOURCES = modindex.c util.c logging.c index.c util.h logging.h index.h

insmod_static_SOURCES = insmod.c
insmod_static_LDFLAGS = -static
# We don't want the $(zlib_flags) here: that makes a dynamic executable
insmod_static_LDADD = 

EXTRA_insmod_SOURCES =
EXTRA_lsmod_SOURCES =
EXTRA_modprobe_SOURCES = 
EXTRA_rmmod_SOURCES =
EXTRA_insmod_static_SOURCES =
EXTRA_depmod_SOURCES = moduleops_core.c
EXTRA_modinfo_SOURCES =

MAN5 = modprobe.conf.5 modules.dep.5 depmod.conf.5
MAN8 = depmod.8 insmod.8 lsmod.8 rmmod.8 modprobe.8 modinfo.8
SGML = $(addprefix doc/,  $(MAN5:%.5=%.sgml) $(MAN8:%.8=%.sgml))
dist_man_MANS = $(MAN5) $(MAN8)
# If they haven't overridden mandir, fix it (never /man!)
mandir =$(shell if [ @mandir@ = $(prefix)/man ]; then if [ $(prefix) = / ]; then echo /usr/share/man; else echo $(prefix)/share/man; fi; else echo @mandir@; fi)

TESTSUITE := $(shell test -e @srcdir@/tests && find @srcdir@/tests -type f ! -name '*~')

EXTRA_DIST = generate-modprobe.conf modprobe.devfs FAQ CODING stress_modules.sh install-with-care $(SGML) $(man_MANS) $(TESTSUITE)

sbin_PROGRAMS = insmod modprobe rmmod depmod modinfo insmod.static
bin_PROGRAMS = lsmod
sbin_SCRIPTS = generate-modprobe.conf
noinst_PROGRAMS=modindex
INSTALL = $(SHELL) $(top_srcdir)/install-with-care

DIST_DIR := /home/rusty/devel/kernel/www/modules/

MAINTAINERCLEANFILES := $(man_MANS)

# docbook2man writes file itself, doesn't do stdout.
%.8: doc/%.sgml
	if [ "$(DOCBOOKTOMAN)" = "docbook2man" ]; then                     \
	    $(DOCBOOKTOMAN) $< > /dev/null 2>&1;                       \
	else                                                               \
	    $(DOCBOOKTOMAN) $< 2>&1 > $@ | sed 's/^[^:]*://';              \
	fi

%.5: doc/%.sgml
	if [ "$(DOCBOOKTOMAN)" = "docbook2man" ]; then                     \
	    $(DOCBOOKTOMAN) $< > /dev/null 2>&1;                       \
	else                                                               \
	    $(DOCBOOKTOMAN) $< 2>&1 > $@ | sed 's/^[^:]*://';              \
	fi

# docbook2man creates extra crap files which need cleaning.
distclean-local: killbuildmanpages

killbuildmanpages:
	rm -f doc/*.tmp manpage.refs manpage.links

tarball: $(DIST_DIR)/module-init-tools-$(VERSION).tar.gz

$(DIST_DIR)/module-init-tools-$(VERSION).tar.gz: dist
	mv module-init-tools-$(VERSION).tar.gz $@

srpm: $(DIST_DIR)/module-init-tools-$(VERSION).tar.gz
	ln -sf $(DIST_DIR)/module-init-tools-$(VERSION).tar.gz /usr/src/rpm/SOURCES/
	set -e; RELEASE=`grep '^Release:' /usr/src/rpm/SPECS/modutils.spec | awk '{ print $$2 }'`; \
	if [ "`grep '%define mit_ver' /usr/src/rpm/SPECS/modutils.spec | awk '{print $$3}'`" != $(VERSION) ]; then \
		sed -e "s/Release: $$RELEASE/Release: `expr $$RELEASE + 1`/" -e 's/%define mit_ver.*/%define mit_ver $(VERSION)/' < /usr/src/rpm/SPECS/modutils.spec > /tmp/modutils.spec; \
		rpm -ba /tmp/modutils.spec; \
		mv /usr/src/rpm/SRPMS/modutils-2.4.21-`expr $$RELEASE + 1`.src.rpm $(DIST_DIR); \
		mv /tmp/modutils.spec /usr/src/rpm/SPECS/modutils.spec; \
	else \
		rpm -ba /usr/src/rpm/SPECS/modutils.spec; \
		mv /usr/src/rpm/SRPMS/modutils-2.4.21-$$RELEASE.src.rpm $(DIST_DIR); \
	fi

# No testsuite?
tests/runtests:
	mkdir tests
	echo '#! /bin/sh' > $@
	echo '# Install the testsuite over this dir. ' >> $@
	chmod a+x $@

$(DIST_DIR)/module-init-tools-testsuite-$(VERSION).tar.gz:
	ln -sfn `pwd` /tmp/module-init-tools-$(VERSION) 
	cd /tmp && tar --exclude '*~' -c -z -f $@ module-init-tools-$(VERSION)/tests
	rm /tmp/module-init-tools-$(VERSION)

release: check clean tarball
