dnl @synopsis MYSQL_C_API_LOCATION
dnl 
dnl This macro tries to find MySQL C API header and library locations.
dnl
dnl We define the following configure script flags:
dnl
dnl		--with-mysql: Give prefix for both library and headers, and try
dnl			to guess subdirectory names for each by tacking common
dnl         suffixes on like /lib and /include.
dnl		--with-mysql-lib: Same as --with-mysql, but for library only.
dnl		--with-mysql-include: Same as --with-mysql, but for headers only.
dnl
dnl @version 1.4, 2009/05/28
dnl @author Warren Young <mysqlpp@etr-usa.com>
AC_DEFUN([MYSQL_C_API_LOCATION],
[
	#
	# Set up configure script macros
	#
	AC_ARG_WITH(mysql,
		[  --with-mysql=<path>     root directory path of MySQL installation],
		[MYSQL_lib_check="$with_mysql/lib/mysql $with_mysql/lib"
		MYSQL_inc_check="$with_mysql/include $with_mysql/include/mysql"],
		[MYSQL_lib_check="/usr/lib64 /usr/lib /usr/lib64/mysql /usr/lib/mysql /usr/local/lib64 /usr/local/lib /usr/local/lib/mysql /usr/local/mysql/lib /usr/local/mysql/lib/mysql /usr/mysql/lib/mysql /opt/mysql/lib /opt/mysql/lib/mysql /sw/lib /sw/lib/mysql"
		MYSQL_inc_check="/usr/include/mysql /usr/local/include/mysql /usr/local/mysql/include /usr/local/mysql/include/mysql /usr/mysql/include/mysql /opt/mysql/include/mysql /sw/include/mysql"])
	AC_ARG_WITH(mysql-lib,
		[  --with-mysql-lib=<path> directory path of MySQL library installation],
		[MYSQL_lib_check="$with_mysql_lib $with_mysql_lib/lib64 $with_mysql_lib/lib $with_mysql_lib/lib64/mysql $with_mysql_lib/lib/mysql"])
	AC_ARG_WITH(mysql-include,
		[  --with-mysql-include=<path> directory path of MySQL header installation],
		[MYSQL_inc_check="$with_mysql_include $with_mysql_include/include $with_mysql_include/include/mysql"])

	#
	# Decide which C API library to use, based on thread support
	#
	if test "x$acx_pthread_ok" = xyes
	then
		MYSQL_C_LIB_NAME=mysqlclient_r
	else
		MYSQL_C_LIB_NAME=mysqlclient
	fi

	#
	# Look for MySQL C API library
	#
	AC_MSG_CHECKING([for MySQL library directory])
	MYSQL_C_LIB_DIR=
	for m in $MYSQL_lib_check
	do
		if test -d "$m" && \
			(test -f "$m/lib$MYSQL_C_LIB_NAME.so" || \
			 test -f "$m/lib$MYSQL_C_LIB_NAME.a")
		then
			MYSQL_C_LIB_DIR=$m
			break
		fi
	done

	if test -z "$MYSQL_C_LIB_DIR"
	then
		AC_MSG_ERROR([Didn't find $MYSQL_C_LIB_NAME library in '$MYSQL_lib_check'])
	fi

	case "$MYSQL_C_LIB_DIR" in
		/* ) ;;
		* )  AC_MSG_ERROR([The MySQL library directory ($MYSQL_C_LIB_DIR) must be an absolute path.]) ;;
	esac

	AC_MSG_RESULT([$MYSQL_C_LIB_DIR])

	case "$MYSQL_C_LIB_DIR" in
	  /usr/lib)
		MYSQL_C_LIB_DIR=
	  	;;
	  *)
	  	LDFLAGS="$LDFLAGS -L${MYSQL_C_LIB_DIR}"
		;;
	esac


	#
	# Look for MySQL C API headers
	#
	AC_MSG_CHECKING([for MySQL include directory])
	MYSQL_C_INC_DIR=
	for m in $MYSQL_inc_check
	do
		if test -d "$m" && test -f "$m/mysql.h"
		then
			MYSQL_C_INC_DIR=$m
			break
		fi
	done

	if test -z "$MYSQL_C_INC_DIR"
	then
		AC_MSG_ERROR([Didn't find the MySQL include dir in '$MYSQL_inc_check'])
	fi

	case "$MYSQL_C_INC_DIR" in
		/* ) ;;
		* )  AC_MSG_ERROR([The MySQL include directory ($MYSQL_C_INC_DIR) must be an absolute path.]) ;;
	esac

	AC_MSG_RESULT([$MYSQL_C_INC_DIR])

	CPPFLAGS="$CPPFLAGS -I${MYSQL_C_INC_DIR}"

    AC_MSG_CHECKING([if we can link to MySQL C API library directly])
	save_LIBS=$LIBS
	LIBS="$LIBS -l$MYSQL_C_LIB_NAME $MYSQLPP_EXTRA_LIBS"
	AC_TRY_LINK(
        [ #include <mysql.h> ],
        [ mysql_store_result(0); ],
        AC_MSG_RESULT([yes]),
        [ AC_MSG_RESULT([no])	
          LIBS="$save_LIBS"
          AC_CHECK_HEADERS(zlib.h, AC_CHECK_LIB(z, gzread, [],
              [ AC_MSG_ERROR([zlib not found]) ]))
          AC_MSG_CHECKING([whether adding -lz will let MySQL C API link succeed])
          MYSQLPP_EXTRA_LIBS="$MYSQLPP_EXTRA_LIBS -lz"
          LIBS="$save_LIBS -l$MYSQL_C_LIB_NAME $MYSQLPP_EXTRA_LIBS"
          AC_TRY_LINK(
              [ #include <mysql.h> ],
              [ mysql_store_result(0); ],
              AC_MSG_RESULT([yes]),
              [ AC_MSG_RESULT([no])
                AC_MSG_ERROR([Unable to link to MySQL client library!])
              ]
          )
        ])
	LIBS=$save_LIBS

	AC_SUBST(MYSQL_C_INC_DIR)
	AC_SUBST(MYSQL_C_LIB_DIR)
	AC_SUBST(MYSQL_C_LIB_NAME)
]) dnl MYSQL_C_API_LOCATION

