AC_DEFUN([TCMALLOC],
	[AC_ARG_WITH(
		[tcmalloc],
		[AS_HELP_STRING([--with-tcmalloc], [use tcmalloc if it's available])],
		[],
		[with_tcmalloc=no])

	AS_IF([test "x$with_tcmalloc" != "xno"],
		[AC_CHECK_LIB([tcmalloc], [tc_cfree], [],
			[AC_MSG_WARN([tcmalloc libraries not installed])]
		)])
])
