
INCLUDES =						\
	-I$(includedir)					\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/intl			 	\
	-I$(top_srcdir)/camel				\
	-I$(top_srcdir)/camel/tests/lib 		\
	-DG_LOG_DOMAIN=\"evolution-tests\"		\
	$(CAMEL_CFLAGS)

LDADD = \
	$(top_builddir)/libedataserver/libedataserver-${API_VERSION}.la \
	$(top_builddir)/camel/tests/lib/libcameltest.a			\
	$(top_builddir)/camel/libcamel-${API_VERSION}.la 		\
	$(INTLLIBS)

check_PROGRAMS =  	\
	test1		\
	test2		\
	test3		\
	test4

CLEANFILES = test3.msg test3-2.msg test3-3.msg

#TESTS = test1 test2 test3 test4
