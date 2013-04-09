AC_PREREQ([2.67])
AC_INIT( [productometer], [1.0.0], [snyder.bradleyj@gmail.com])
AM_INIT_AUTOMAKE(productometer, 1.0.0)
AC_OUTPUT(Makefile src/Makefile)

AC_CONFIG_HEADERS([config.h])
#AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([mysqlclient], [mysql_init], [], [AC_MSG_ERROR([libmysqlclient not found])] )
AC_CHECK_LIB([pcre], [pcre_exec])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h sys/time.h])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_OUTPUT
