# Copyright (c) 2013-2015 Mellanox Technologies, Inc.
#                         All rights reserved.
# Copyright (c) 2017      IBM Corporation.  All rights reserved.
# $COPYRIGHT$
#
#  Additional copyrights may follow
#
# $HEADER$
#
#

dist_oshmemdata_DATA = \
	help-oshmem-scoll-mpi.txt

scoll_ucx_sources = \
					scoll_ucx.h \
					scoll_ucx_debug.h \
					scoll_ucx_dtypes.h \
					scoll_ucx_module.c \
					scoll_ucx_component.c \
					scoll_ucx_ops.c
if MCA_BUILD_oshmem_scoll_ucx_DSO
component_noinst =
component_install = mca_scoll_ucx.la
else
component_noinst = libmca_scoll_ucx.la
component_install =
endif

mcacomponentdir = $(pkglibdir)
mcacomponent_LTLIBRARIES = $(component_install)
mca_scoll_ucx_la_SOURCES = $(scoll_ucx_sources)
mca_scoll_ucx_la_LIBADD = $(top_builddir)/oshmem/liboshmem.la \
	$(scoll_ucx_LIBS)
mca_scoll_ucx_la_LDFLAGS = -module -avoid-version $(scoll_ucx_LDFLAGS)

noinst_LTLIBRARIES = $(component_noinst)
libmca_scoll_ucx_la_SOURCES =$(scoll_ucx_sources)
libmca_scoll_ucx_la_LIBADD = $(scoll_ucx_LIBS)
libmca_scoll_ucx_la_LDFLAGS = -module -avoid-version $(scoll_ucx_LDFLAGS)
