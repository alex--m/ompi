# -*- shell-script -*-
#
#
# Copyright (c) 2011 Mellanox Technologies. All rights reserved.
# Copyright (c) 2020 Huawei Technologies Co., Ltd. All rights reserved.
# $COPYRIGHT$
#
# Additional copyrights may follow
#
# $HEADER$
#

AC_DEFUN([MCA_ompi_coll_ucc_POST_CONFIG], [
    AS_IF([test "$1" = "1"], [OMPI_REQUIRE_ENDPOINT_TAG([UCC])])
])

# MCA_coll_ucc_CONFIG([action-if-can-compile],
#                     [action-if-cant-compile])
# ------------------------------------------------
AC_DEFUN([MCA_ompi_coll_ucc_CONFIG],[
    AC_CONFIG_FILES([ompi/mca/coll/ucc/Makefile])

    OMPI_CHECK_UCC([coll_ucc],
                   [coll_ucc_happy="yes"],
                   [coll_ucc_happy="no"])

    AS_IF([test "$coll_ucc_happy" = "yes"],
          [$1],
          [$2])

    # substitute in the things needed to build ucc
    AC_SUBST([coll_ucc_CFLAGS])
    AC_SUBST([coll_ucc_CPPFLAGS])
    AC_SUBST([coll_ucc_LDFLAGS])
    AC_SUBST([coll_ucc_LIBS])
])dnl
