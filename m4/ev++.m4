dnl @synopsis EV_DEVEL
dnl 
dnl This macro tries to find the libev library and header files.
dnl
dnl We define the following configure script flags:
dnl
dnl		--with-ev: Give prefix for both library and headers, and try
dnl			to guess subdirectory names for each.  (e.g. tack /lib and
dnl			/include onto given dir name, and other common schemes.)
dnl		--with-ev-lib: Similar to --with-ev, but for library only.
dnl		--with-ev-include: Similar to --with-ev, but for headers
dnl			only.

AC_DEFUN([EV_DEVEL],
[
	dnl
	dnl Set up configure script macros
	dnl
	AC_ARG_WITH(ev,
		[AS_HELP_STRING([--with-ev=<path>],[path containing libev header and library subdirs])],
		[EV_lib_check="$with_ev/lib64 $with_ev/lib $with_ev/lib64/libev $with_ev/lib/libev"
		  EV_inc_check="$with_ev/include $with_ev/include/libev"],
		[EV_lib_check="/usr/local/libev/lib64 /usr/local/libev/lib /usr/local/lib64/libev /usr/local/lib/libev /opt/libev/lib64 /opt/libev/lib /usr/lib64/libev /usr/lib/libev /usr/local/lib64 /usr/local/lib /usr/lib64 /usr/lib"
		  EV_inc_check="/usr/local/libev/include /usr/local/include/libev /opt/libev/include /usr/local/include/libev /usr/local/include /usr/include/libev /usr/include"])
	AC_ARG_WITH(ev-lib,
		[AS_HELP_STRING([--with-ev-lib=<path>],[directory path of libev library])],
		[EV_lib_check="$with_ev_lib $with_ev_lib/lib64 $with_ev_lib/lib $with_ev_lib/lib64/libev $with_ev_lib/lib/libev"])
	AC_ARG_WITH(ev-include,
		[AS_HELP_STRING([--with-ev-include=<path>],[directory path of libev headers])],
		[EV_inc_check="$with_ev_include $with_ev_include/include $with_ev_include/include/libev"])

	dnl
	dnl Look for libev library
	dnl
	AC_CACHE_CHECK([for libev library location], [ac_cv_ev_lib],
	[
		for dir in $EV_lib_check
		do
			if test -d "$dir" && \
				( test -f "$dir/libev.so" ||
				  test -f "$dir/libev.a" )
			then
				ac_cv_ev_lib=$dir
				break
			fi
		done

		if test -z "$ac_cv_ev_lib"
		then
			AC_MSG_RESULT([no])
			AC_MSG_ERROR([Didn't find the libev library dir in '$EV_lib_check'])
		fi

		case "$ac_cv_ev_lib" in
			/* )
				;;
			* )
				AC_MSG_RESULT([])
				AC_MSG_ERROR([The libev library directory ($ac_cv_ev_lib) must be an absolute path.])
				;;
		esac
	])
	AC_SUBST([EV_LIB_DIR],[$ac_cv_ev_lib])

	dnl
	dnl Look for libev header file directory
	dnl
	AC_CACHE_CHECK([for libev include path], [ac_cv_ev_inc],
	[
		for dir in $EV_inc_check
		do
			if test -d "$dir" && test -f "$dir/ev++.h"
			then
				ac_cv_ev_inc=$dir
				break
			fi
		done

		if test -z "$ac_cv_ev_inc"
		then
			AC_MSG_RESULT([no])
			AC_MSG_ERROR([Didn't find the libev header dir in '$EV_inc_check'])
		fi

		case "$ac_cv_ev_inc" in
			/* )
				;;
			* )
				AC_MSG_RESULT([])
				AC_MSG_ERROR([The libev header directory ($ac_cv_ev_inc) must be an absolute path.])
				;;
		esac
	])
	AC_SUBST([EV_INC_DIR],[$ac_cv_ev_inc])

	dnl
	dnl Now check that the above checks resulted in -I and -L flags that
	dnl let us build actual programs against libev.
	dnl
	case "$ac_cv_ev_lib" in
		/usr/lib)
			;;
		*)
			LDFLAGS="$LDFLAGS -L${ac_cv_ev_lib}"
			;;
	esac
	CPPFLAGS="$CPPFLAGS -I${ac_cv_ev_inc}"
	AC_MSG_CHECKING([that we can build libev programs])
	AC_EGREP_CPP(
		[bad_gcc_libev_ver],
		[
#include <ev.h>
#if __GNUC__ && (__GNUC__ < 4 || __GNUC__ == 4 && __GNUC_MINOR__ < 9) && EV_VERSION_MAJOR == 4 && EV_VERSION_MINOR == 18
	bad_gcc_libev_ver
#endif
		],
		[AC_MSG_RESULT([no])
			AC_MSG_ERROR([GCC versions prior to 4.9 cannot compile libev 4.18])],
		[],
	)

	LIBS="-lev $LIBS"
	AC_LANG_PUSH([C++])
	AC_LINK_IFELSE(
		[AC_LANG_PROGRAM(
			[#include <ev++.h>],
			[ev::io i])
		],
		[AC_MSG_RESULT([yes])],
		[AC_MSG_RESULT([no])
			AC_MSG_FAILURE([Cannot build libev programs])]
	)
	AC_LANG_POP([C++])
]) dnl End EV_DEVEL

