#-######################################################################
# mysql++.m4 - Example autoconf macro showing how to find MySQL++
#	library and header files.
#
# Copyright (c) 2004-2009 by Educational Technology Resources, Inc.
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License as published
# by the Free Software Foundation; either version 2.1 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with MySQL++; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
# USA
#-######################################################################

dnl @synopsis MYSQLPP_DEVEL
dnl 
dnl This macro tries to find the MySQL++ library and header files.
dnl
dnl We define the following configure script flags:
dnl
dnl		--with-mysqlpp: Give prefix for both library and headers, and try
dnl			to guess subdirectory names for each.  (e.g. tack /lib and
dnl			/include onto given dir name, and other common schemes.)
dnl		--with-mysqlpp-lib: Similar to --with-mysqlpp, but for library only.
dnl		--with-mysqlpp-include: Similar to --with-mysqlpp, but for headers
dnl			only.
dnl
dnl This macro depends on having the default compiler and linker flags
dnl set up for building programs against the MySQL C API.  The mysql.m4
dnl macro in this directory fits this bill; run it first.
dnl
dnl @version 1.3, 2009/11/22
dnl @author Warren Young <mysqlpp@etr-usa.com>

AC_DEFUN([MYSQLPP_DEVEL],
[
	dnl
	dnl Set up configure script macros
	dnl
	AC_ARG_WITH(mysqlpp,
		[  --with-mysqlpp=<path>     path containing MySQL++ header and library subdirs],
		[MYSQLPP_lib_check="$with_mysqlpp/lib64 $with_mysqlpp/lib $with_mysqlpp/lib64/mysql++ $with_mysqlpp/lib/mysql++"
		  MYSQLPP_inc_check="$with_mysqlpp/include $with_mysqlpp/include/mysql++"],
		[MYSQLPP_lib_check="/usr/local/mysql++/lib64 /usr/local/mysql++/lib /usr/local/lib64/mysql++ /usr/local/lib/mysql++ /opt/mysql++/lib64 /opt/mysql++/lib /usr/lib64/mysql++ /usr/lib/mysql++ /usr/local/lib64 /usr/local/lib /usr/lib64 /usr/lib"
		  MYSQLPP_inc_check="/usr/local/mysql++/include /usr/local/include/mysql++ /opt/mysql++/include /usr/local/include/mysql++ /usr/local/include /usr/include/mysql++ /usr/include"])
	AC_ARG_WITH(mysqlpp-lib,
		[  --with-mysqlpp-lib=<path> directory path of MySQL++ library],
		[MYSQLPP_lib_check="$with_mysqlpp_lib $with_mysqlpp_lib/lib64 $with_mysqlpp_lib/lib $with_mysqlpp_lib/lib64/mysql $with_mysqlpp_lib/lib/mysql"])
	AC_ARG_WITH(mysqlpp-include,
		[  --with-mysqlpp-include=<path> directory path of MySQL++ headers],
		[MYSQLPP_inc_check="$with_mysqlpp_include $with_mysqlpp_include/include $with_mysqlpp_include/include/mysql"])

	dnl
	dnl Look for MySQL++ library
	dnl
	AC_CACHE_CHECK([for MySQL++ library location], [ac_cv_mysqlpp_lib],
	[
		for dir in $MYSQLPP_lib_check
		do
			if test -d "$dir" && \
				( test -f "$dir/libmysqlpp.so" ||
				  test -f "$dir/libmysqlpp.a" )
			then
				ac_cv_mysqlpp_lib=$dir
				break
			fi
		done

		if test -z "$ac_cv_mysqlpp_lib"
		then
			AC_MSG_ERROR([Didn't find the MySQL++ library dir in '$MYSQLPP_lib_check'])
		fi

		case "$ac_cv_mysqlpp_lib" in
			/* ) ;;
			* )  AC_MSG_ERROR([The MySQL++ library directory ($ac_cv_mysqlpp_lib) must be an absolute path.]) ;;
		esac
	])
	AC_SUBST([MYSQLPP_LIB_DIR],[$ac_cv_mysqlpp_lib])

	dnl
	dnl Look for MySQL++ header file directory
	dnl
	AC_CACHE_CHECK([for MySQL++ include path], [ac_cv_mysqlpp_inc],
	[
		for dir in $MYSQLPP_inc_check
		do
			if test -d "$dir" && test -f "$dir/mysql++.h"
			then
				ac_cv_mysqlpp_inc=$dir
				break
			fi
		done

		if test -z "$ac_cv_mysqlpp_inc"
		then
			AC_MSG_ERROR([Didn't find the MySQL++ header dir in '$MYSQLPP_inc_check'])
		fi

		case "$ac_cv_mysqlpp_inc" in
			/* ) ;;
			* )  AC_MSG_ERROR([The MySQL++ header directory ($ac_cv_mysqlpp_inc) must be an absolute path.]) ;;
		esac
	])
	AC_SUBST([MYSQLPP_INC_DIR],[$ac_cv_mysqlpp_inc])

	dnl
	dnl Now check that the above checks resulted in -I and -L flags that
	dnl let us build actual programs against MySQL++.
	dnl
	case "$ac_cv_mysqlpp_lib" in
	  /usr/lib) ;;
	  *) LDFLAGS="$LDFLAGS -L${ac_cv_mysqlpp_lib}" ;;
	esac
	CPPFLAGS="$CPPFLAGS -I${ac_cv_mysqlpp_inc} -I${MYSQL_C_INC_DIR}"
	AC_MSG_CHECKING([that we can build MySQL++ programs])
	AC_COMPILE_IFELSE(
		[AC_LANG_PROGRAM([#include <mysql++.h>],
			[mysqlpp::Connection c(false)])],
		AC_MSG_RESULT([yes]),
		AC_MSG_ERROR([no]))
]) dnl End MYSQLPP_DEVEL

