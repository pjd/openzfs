uzpoold_CPPFLAGS = $(AM_CPPFLAGS) $(FORCEDEBUG_CPPFLAGS)
uzpoold_CFLAGS   = $(AM_CFLAGS) $(LIBCRYPTO_CFLAGS)

sbin_PROGRAMS   += uzpoold
CPPCHECKTARGETS += uzpoold

uzpoold_SOURCES = \
	%D%/uzpoold.c

uzpoold_LDADD = \
	libzpool.la \
	libzfs_core.la \
	libnvpair.la

uzpoold_LDADD += $(LIBCRYPTO_LIBS)
