# -*- shell-script -*-
#
# Copyright (C) 2015-2017 Mellanox Technologies, Inc.
#                         All rights reserved.
# Copyright (c) 2015      Research Organization for Information Science
#                         and Technology (RIST). All rights reserved.
# Copyright (c) 2016      Los Alamos National Security, LLC. All rights
#                         reserved.
# Copyright (c) 2016 Cisco Systems, Inc.  All rights reserved.
# Copyright (c) 2020 Huawei Technologies Co., Ltd.  All rights reserved.
# $COPYRIGHT$
#
# Additional copyrights may follow
#
# $HEADER$
#

# OMPI_CHECK_UCC(prefix, [action-if-found], [action-if-not-found])
# --------------------------------------------------------
# check if UCC support can be found.  sets prefix_{CPPFLAGS,
# LDFLAGS, LIBS} as needed and runs action-if-found if there is
# support, otherwise executes action-if-not-found
AC_DEFUN([OMPI_CHECK_UCC],[
    OPAL_VAR_SCOPE_PUSH([ompi_check_ucc_dir])

    AS_IF([test -z "$ompi_check_ucc_happy"],
          [AC_ARG_WITH([ucc],
                       [AC_HELP_STRING([--with-ucc(=DIR)],
                       [Build with Unified Collectives library support])])
       OPAL_CHECK_WITHDIR([ucc], [$with_ucc], [include/ucc/api/ucc.h])
       AC_ARG_WITH([ucc-libdir],
                   [AC_HELP_STRING([--with-ucc-libdir=DIR],
                                   [Search for Unified Collectives library in DIR])])
       OPAL_CHECK_WITHDIR([ucc-libdir], [$with_ucc_libdir], [libucc.*])

       AS_IF([test "$with_ucc" != "no"],
                 [AS_IF([test -n "$with_ucc" && test "$with_ucc" != "yes"],
                        [ompi_check_ucc_dir="$with_ucc"],
                        [PKG_CHECK_MODULES_STATIC([ucc],[ucc],
                                                  [ompi_check_ucc_dir=`$PKG_CONFIG --variable=prefix ucc`
                                                   AS_IF([test "$ompi_check_ucc_dir" = "/usr"],
                                                         [ompi_check_ucc_dir=])],
                                                         [true])])
                  ompi_check_ucc_happy="no"
                  AS_IF([test -z "$ompi_check_ucc_dir"],
                        [OPAL_CHECK_PACKAGE([ompi_check_ucc],
                                   [ucc/api/ucc.h],
                                   [ucc],
                                   [ucc_finalize],
                                   [-lucc],
                                   [],
                                   [],
                                   [ompi_check_ucc_happy="yes"],
                                   [ompi_check_ucc_happy="no"])
                         AS_IF([test "$ompi_check_ucc_happy" = yes],
                               [AC_MSG_CHECKING(for UCC version compatibility)
                                AC_REQUIRE_CPP
                                AC_COMPILE_IFELSE(
                                    [AC_LANG_PROGRAM([[#include <ucc/api/version.h>]],[[]])],
                                    [ompi_check_ucc_happy="yes"],
                                    [ompi_check_ucc_happy="no"])

                                AC_MSG_RESULT([$ompi_check_ucc_happy])])
                         AS_IF([test "$ompi_check_ucc_happy" = "no"],
                               [ompi_check_ucc_dir=/opt/ucc])])
                  AS_IF([test "$ompi_check_ucc_happy" != yes],
                        [AS_IF([test -n "$with_ucc_libdir"],
                               [ompi_check_ucc_libdir="$with_ucc_libdir"],
                               [files=`ls $ompi_check_ucc_dir/lib64/libucc.* 2> /dev/null | wc -l`
                                AS_IF([test "$files" -gt 0],
                                      [ompi_check_ucc_libdir=$ompi_check_ucc_dir/lib64],
                                      [ompi_check_ucc_libdir=$ompi_check_ucc_dir/lib])])

                         ompi_check_ucc_$1_save_CPPFLAGS="$CPPFLAGS"
                         ompi_check_ucc_$1_save_LDFLAGS="$LDFLAGS"
                         ompi_check_ucc_$1_save_LIBS="$LIBS"

                         OPAL_CHECK_PACKAGE([ompi_check_ucc],
                                            [ucc/api/ucc.h],
                                            [ucc],
                                            [ucc_finalize],
                                            [-lucc],
                                            [$ompi_check_ucc_dir],
                                            [$ompi_check_ucc_libdir],
                                            [ompi_check_ucc_happy="yes"],
                                            [ompi_check_ucc_happy="no"])

                         CPPFLAGS="$ompi_check_ucc_$1_save_CPPFLAGS"
                         LDFLAGS="$ompi_check_ucc_$1_save_LDFLAGS"
                         LIBS="$ompi_check_ucc_$1_save_LIBS"])])])

    AS_IF([test "$ompi_check_ucc_happy" = "yes"],
          [$1_CPPFLAGS="[$]$1_CPPFLAGS $ompi_check_ucc_CPPFLAGS"
           $1_LDFLAGS="[$]$1_LDFLAGS $ompi_check_ucc_LDFLAGS"
           $1_LIBS="[$]$1_LIBS $ompi_check_ucc_LIBS"
           $2
           AC_DEFINE([HAVE_UCC],[1],[Unified Collective Communication (UCC) enabled])],
          [AS_IF([test ! -z "$with_ucc" && test "$with_ucc" != "no"],
                 [AC_MSG_ERROR([UCC support requested but not found.  Aborting])])
           $3])

    OPAL_VAR_SCOPE_POP
])

