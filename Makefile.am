ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = ocelot
ocelot_SOURCES = config.cpp db.cpp events.cpp logger.cpp misc_functions.cpp ocelot.cpp \
	schedule.cpp site_comm.cpp worker.cpp

AM_CPPFLAGS = -march=native -O2 -fvisibility=hidden -fvisibility-inlines-hidden -fomit-frame-pointer -fno-ident -pthread $(BOOST_CPPFLAGS) -I$(MYSQLPP_INC_DIR) -I$(EV_INCLUDE_DIR)
ocelot_LDFLAGS = -pthread $(BOOST_LDFLAGS) -L$(EV_LIB_DIR) -L$(MYSQLPP_LIB_DIR)
ocelot_LDADD = $(BOOST_IOSTREAMS_LIB) $(BOOST_SYSTEM_LIB) $(BOOST_THREAD_LIB) -lev -lmysqlpp
