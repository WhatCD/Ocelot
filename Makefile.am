ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = ocelot
ocelot_SOURCES = config.cpp config.h db.cpp db.h events.cpp events.h misc_functions.cpp misc_functions.h \
	ocelot.cpp ocelot.h report.cpp report.h response.cpp response.h \
	schedule.cpp schedule.h site_comm.cpp site_comm.h user.cpp user.h worker.cpp worker.h

AM_CXXFLAGS = -std=c++11 -march=native -O2 -fvisibility=hidden -fvisibility-inlines-hidden -fomit-frame-pointer -fno-ident -Wall -Wfatal-errors $(PTHREAD_CFLAGS) $(BOOST_CPPFLAGS)
ocelot_LDADD = $(PTHREAD_LIBS) $(BOOST_IOSTREAMS_LIB) $(BOOST_SYSTEM_LIB)
AM_LDFLAGS = -Wl,-O1 -Wl,--as-needed
EXTRA_DIST = CHANGES LICENSE README.md ocelot.conf.dist
dist-hook:
	touch ${distdir}/configure
	patch -p2 -d ${distdir} --no-backup-if-mismatch < ../dist.patch
