# -*- shell-script -*-
#
#
# Copyright (c) 2011 Mellanox Technologies. All rights reserved.
# Copyright (c) 2013 Cisco Systems, Inc.  All rights reserved.
# Copyright (c) 2020 Huawei Technologies Co., Ltd. All rights reserved.
# $COPYRIGHT$
#
# Additional copyrights may follow
#
# $HEADER$
#
#

AM_CPPFLAGS = $(coll_ucc_CPPFLAGS) $(common_ucx_CPPFLAGS) -DCOLL_UCC_HOME=\"$(coll_ucc_HOME)\" $(coll_ucc_extra_CPPFLAGS)

#dist_ompidata_DATA = help-coll-ucc.txt
coll_ucc_sources = \
        coll_ucc.h \
        coll_ucc_op.c \
        coll_ucc_module.c \
        coll_ucc_component.c

# Make the output library in this directory, and name it either
# mca_<type>_<name>.la (for DSO builds) or libmca_<type>_<name>.la
# (for static builds).

if MCA_BUILD_ompi_coll_ucc_DSO
component_noinst =
component_install = mca_coll_ucc.la
else
component_noinst = libmca_coll_ucc.la
component_install =
endif

mcacomponentdir = $(ompilibdir)
mcacomponent_LTLIBRARIES = $(component_install)
mca_coll_ucc_la_SOURCES = $(coll_ucc_sources)
mca_coll_ucc_la_LIBADD = $(top_builddir)/ompi/lib@OMPI_LIBMPI_NAME@.la $(coll_ucc_LIBS) \
                         $(OMPI_TOP_BUILDDIR)/ompi/mca/common/ucx/libmca_common_ompi_ucx.la
mca_coll_ucc_la_LDFLAGS = -module -avoid-version $(coll_ucc_LDFLAGS) $(common_ucx_LDFLAGS)

noinst_LTLIBRARIES = $(component_noinst)
libmca_coll_ucc_la_SOURCES =$(coll_ucc_sources)
libmca_coll_ucc_la_LIBADD = $(coll_ucc_LIBS)
libmca_coll_ucc_la_LDFLAGS = -module -avoid-version $(coll_ucc_LDFLAGS) $(common_ucx_LDFLAGS)
