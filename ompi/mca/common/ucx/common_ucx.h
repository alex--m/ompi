/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef MCA_COMMON_UCX_H
#define MCA_COMMON_UCX_H

#include "common_ucx_freelist.h"
#include "common_ucx_datatype.h"

#include "ompi/datatype/ompi_datatype.h"
#include "ompi/datatype/ompi_datatype_internal.h"
#include "opal/mca/common/ucx/common_ucx.h"

int mca_common_ucx_open(const char *prefix, size_t *request_size);
int mca_common_ucx_close(void);
int mca_common_ucx_init(mca_base_component_t **version);
int mca_common_ucx_cleanup(void);

#endif /* MCA_COMMON_UCX_H */
