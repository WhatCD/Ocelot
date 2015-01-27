AC_INIT(ocelot, 1.0)
AM_INIT_AUTOMAKE([1.11 no-define foreign])
AC_PROG_CXX
AC_CONFIG_FILES([Makefile])
AC_LANG_PUSH([C++])
AX_CHECK_COMPILE_FLAG([-std=c++11], [], [AC_MSG_ERROR([Compiler does not support -std=c++11])])
AX_CHECK_COMPILE_FLAG([-fstack-protector-strong], [CXXFLAGS="-fstack-protector-strong $CXXFLAGS"])
AC_LANG_POP([C++])
AX_BOOST_BASE([1.37], [], [AC_MSG_ERROR(Need boost >= 1.37)])
AX_BOOST_IOSTREAMS
AX_BOOST_SYSTEM
AX_PTHREAD([], AC_MSG_FAILURE([pthread library is required]))
EV_DEVEL
MYSQL_C_API_LOCATION
MYSQLPP_DEVEL
TCMALLOC

AC_OUTPUT
