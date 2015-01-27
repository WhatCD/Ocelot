AC_DEFUN([TCMALLOC],
	[AC_ARG_WITH(
		[tcmalloc],
		[AS_HELP_STRING([--with-tcmalloc], [use tcmalloc if it's available [default=yes]])],
		[],
		[with_tcmalloc=yes])

	AS_IF([test "x$with_tcmalloc" != "xno"],
		[AC_CHECK_LIB([tcmalloc], [tc_free])]
	)]
)
