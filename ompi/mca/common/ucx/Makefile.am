#
# Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
#                         University Research and Technology
#                         Corporation.  All rights reserved.
# Copyright (c) 2004-2007 The University of Tennessee and The University
#                         of Tennessee Research Foundation.  All rights
#                         reserved.
# Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
#                         University of Stuttgart.  All rights reserved.
# Copyright (c) 2004-2005 The Regents of the University of California.
#                         All rights reserved.
# Copyright (c) 2008-2018 University of Houston. All rights reserved.
# Copyright (c) 2016      IBM Corporation.  All rights reserved.
# Copyright (c) 2017-2018 Research Organization for Information Science
#                         and Technology (RIST). All rights reserved.
# Copyright (c) 2020      Huawei Technologies Co., Ltd.  All rights
#                         reserved.
#
# $COPYRIGHT$
#
# Additional copyrights may follow
#
# $HEADER$
#

headers = \
	common_ucx_freelist.h \
	common_ucx_datatype.h \
	common_ucx.h

sources = \
	common_ucx_datatype.c \
	common_ucx.c


# To simplify components that link to this library, we will *always*
# have an output libtool library named libmca_<type>_<name>.la -- even
# for case 2) described above (i.e., so there's no conditional logic
# necessary in component Makefile.am's that link to this library).
# Hence, if we're creating a noinst version of this library (i.e.,
# case 2), we sym link it to the libmca_<type>_<name>.la name
# (libtool will do the Right Things under the covers).  See the
# all-local and clean-local rules, below, for how this is effected.

lib_LTLIBRARIES =
noinst_LTLIBRARIES =
comp_inst = libmca_common_ucx.la
comp_noinst = libmca_common_ucx_noinst.la

if MCA_BUILD_ompi_common_ucx_DSO
lib_LTLIBRARIES += $(comp_inst)
else
noinst_LTLIBRARIES += $(comp_noinst)
endif

libmca_common_ucx_la_SOURCES = $(headers) $(sources)
libmca_common_ucx_la_CPPFLAGS = $(common_ucx_CPPFLAGS)
libmca_common_ucx_la_LDFLAGS =  \
        -version-info $(libmca_ompi_common_ucx_so_version) \
        $(common_ucx_LDFLAGS)
libmca_common_ucx_la_LIBADD = \
        $(common_ucx_LIBS) \
        $(OPAL_TOP_BUILDDIR)/opal/mca/common/ucx/lib@OPAL_LIB_NAME@mca_common_ucx.la
libmca_common_ucx_noinst_la_SOURCES = $(headers) $(sources)
libmca_common_ucx_noinst_la_CPPFLAGS = $(common_ucx_CPPFLAGS)
libmca_common_ucx_noinst_la_LDFLAGS = $(common_ucx_LDFLAGS)
libmca_common_ucx_noinst_la_LIBADD = $(common_ucx_LIBS) \
	$(OPAL_TOP_BUILDDIR)/opal/mca/common/ucx/lib@OPAL_LIB_NAME@mca_common_ucx.la

# Conditionally install the header files

if WANT_INSTALL_HEADERS
ompidir = $(ompiincludedir)/ompi/mca/common/ucx
ompi_HEADERS = $(headers)
else
ompidir = $(includedir)
endif


# These two rules will sym link the "noinst" libtool library filename
# to the installable libtool library filename in the case where we are
# compiling this component statically (case 2), described above).
V=0
OMPI_V_LN_SCOMP = $(ompi__v_LN_SCOMP_$V)
ompi__v_LN_SCOMP_ = $(ompi__v_LN_SCOMP_$AM_DEFAULT_VERBOSITY)
ompi__v_LN_SCOMP_0 = @echo "  LN_S    " `basename $(comp_inst)`;

all-local:
	$(OMPI_V_LN_SCOMP) if test -z "$(lib_LTLIBRARIES)"; then \
	  rm -f "$(comp_inst)"; \
	  $(LN_S) "$(comp_noinst)" "$(comp_inst)"; \
	fi

clean-local:
	if test -z "$(lib_LTLIBRARIES)"; then \
	  rm -f "$(comp_inst)"; \
	fi
