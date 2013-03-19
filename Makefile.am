ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = ocelot
ocelot_SOURCES = config.h db.cpp db.h events.cpp events.h \
	misc_functions.cpp misc_functions.h ocelot.cpp ocelot.h \
	schedule.cpp schedule.h site_comm.cpp site_comm.h worker.cpp worker.h
nodist_ocelot_SOURCES = config.cpp

AM_CPPFLAGS = -march=native -O2 -fvisibility=hidden -fvisibility-inlines-hidden -fomit-frame-pointer -fno-ident -pthread $(BOOST_CPPFLAGS) -I$(MYSQLPP_INC_DIR) -I$(EV_INCLUDE_DIR)
ocelot_LDFLAGS = -pthread $(BOOST_LDFLAGS) -L$(EV_LIB_DIR) -L$(MYSQLPP_LIB_DIR)
ocelot_LDADD = $(BOOST_IOSTREAMS_LIB) $(BOOST_SYSTEM_LIB) $(BOOST_THREAD_LIB) -lev -lmysqlpp
EXTRA_DIST = CHANGES LICENSE config.cpp.template
