AC_INIT(ocelot, 0.6)
AM_INIT_AUTOMAKE([1.11 no-define foreign])
CXXFLAGS="$CXXFLAGS -std=c++0x -march=native -O2 -fomit-frame-pointer -fno-ident -fvisibility-inlines-hidden -fvisibility=hidden -Wall -Wfatal-errors -iquote -Wl,O1 -Wl,--as-needed"
AC_PROG_CXX
AC_CONFIG_FILES([Makefile])

AX_BOOST_BASE([1.37], [], [AC_MSG_ERROR("Need boost >= 1.37")])
AX_BOOST_IOSTREAMS
AX_BOOST_SYSTEM
AX_BOOST_THREAD
MYSQL_C_API_LOCATION
AC_LANG_PUSH(C++)
MYSQLPP_DEVEL
EV_DEVEL
TCMALLOC
AC_LANG_POP(C++)

AC_OUTPUT
