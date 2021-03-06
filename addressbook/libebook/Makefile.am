INCLUDES =						\
	-DEVOLUTION_LOCALEDIR=\""$(localedir)"\"	\
	-DG_LOG_DOMAIN=\"libebook\"			\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)/addressbook			\
	-I$(top_builddir)/addressbook/libebook		\
        $(EVOLUTION_ADDRESSBOOK_CFLAGS)

# The corba stubs and skels
CORBA_GENERATED_C =				\
	Evolution-DataServer-Addressbook-common.c		\
	Evolution-DataServer-Addressbook-skels.c		\
	Evolution-DataServer-Addressbook-stubs.c
CORBA_GENERATED_H =			\
	Evolution-DataServer-Addressbook.h

CORBA_GENERATED = $(CORBA_GENERATED_C) $(CORBA_GENERATED_H)

idls =						\
	$(srcdir)/../idl/Evolution-DataServer-Addressbook.idl

idl_flags = $(IDL_INCLUDES)

$(CORBA_GENERATED_H): $(idls)
	$(ORBIT_IDL) $(idl_flags) $(srcdir)/../idl/Evolution-DataServer-Addressbook.idl
$(CORBA_GENERATED_C): $(CORBA_GENERATED_H)

# The library
lib_LTLIBRARIES = libebook-1.2.la

libebook_1_2_la_SOURCES =				\
	$(CORBA_GENERATED_C)				\
	$(CORBA_GENERATED_H)				\
	e-address-western.c				\
	e-book-listener.c				\
	e-book-listener.h				\
	e-book-query.c					\
	e-book-view-listener.c				\
	e-book-view-listener.h				\
	e-book-view-private.h				\
	e-book-view.c					\
	e-book.c					\
	e-contact.c					\
	e-destination.c					\
	e-name-western.c				\
	e-name-western-tables.h                         \
	e-vcard.c

libebook_1_2_la_LIBADD =					\
	$(EVOLUTION_ADDRESSBOOK_LIBS)				\
	$(top_builddir)/camel/libcamel-1.2.la			\
	$(top_builddir)/libedataserver/libedataserver-1.2.la

libebook_1_2_la_LDFLAGS = 							\
	-version-info $(LIBEBOOK_CURRENT):$(LIBEBOOK_REVISION):$(LIBEBOOK_AGE) $(NO_UNDEFINED)

libebookincludedir = $(privincludedir)/libebook

libebookinclude_HEADERS =				\
	e-address-western.h				\
	e-book-query.h					\
	e-book-view.h					\
	e-book-types.h					\
	e-book.h					\
	e-contact.h					\
	e-destination.h					\
	e-name-western.h				\
	e-vcard.h

%-$(API_VERSION).pc: %.pc
	 cp $< $@

e-name-western-tables.h: e-name-western-tables.h.in
	${srcdir}/gen-western-table.py < $< > $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libebook-$(API_VERSION).pc

BUILT_SOURCES = $(CORBA_GENERATED)
CLEANFILES    = $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST = 						\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)     \
	e-name-western-tables.h.in                      \
	gen-western-table.py

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)
