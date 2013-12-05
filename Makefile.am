ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = ocelot
ocelot_SOURCES = config.h db.cpp db.h events.cpp events.h misc_functions.cpp misc_functions.h\
	ocelot.cpp ocelot.h report.cpp report.h response.cpp response.h \
	schedule.cpp schedule.h site_comm.cpp site_comm.h user.cpp user.h worker.cpp worker.h
nodist_ocelot_SOURCES = config.cpp

AM_CPPFLAGS = -std=c++11 -march=native -O2 -fvisibility=hidden -fvisibility-inlines-hidden -fomit-frame-pointer -fno-ident -pthread -Wall -Wfatal-errors -Wl,O1 -Wl,--as-needed $(BOOST_CPPFLAGS)
ocelot_LDFLAGS = -pthread $(BOOST_LDFLAGS)
ocelot_LDADD = $(BOOST_IOSTREAMS_LIB) $(BOOST_SYSTEM_LIB) -lev -lmysqlpp
EXTRA_DIST = CHANGES LICENSE config.cpp.template
dist-hook:
	touch ${distdir}/configure
	patch -p2 -d ${distdir} < ../dist.patch
