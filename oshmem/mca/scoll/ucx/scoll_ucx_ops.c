/**
  Copyright (c) 2011 Mellanox Technologies. All rights reserved.
  Copyright (c) 2017      IBM Corporation.  All rights reserved.
  $COPYRIGHT$

  Additional copyrights may follow

  $HEADER$
 */

#include "ompi_config.h"
#include "ompi/constants.h"

#include "scoll_ucx.h"
#include "scoll_ucx_dtypes.h"

int mca_scoll_ucx_barrier(struct oshmem_group_t *group, long *pSync, int alg)
{
    return OSHMEM_ERR_NOT_IMPLEMENTED;
}

int mca_scoll_ucx_broadcast(struct oshmem_group_t *group,
                            int PE_root,
                            void *target,
                            const void *source,
                            size_t nlong,
                            long *pSync,
                            bool nlong_type,
                            int alg)
{
    return OSHMEM_ERR_NOT_IMPLEMENTED;
}


int mca_scoll_ucx_reduce(struct oshmem_group_t *group,
        struct oshmem_op_t *op,
        void *target,
        const void *source,
        size_t nlong,
        long *pSync,
        void *pWrk,
        int alg)
{
    return OSHMEM_ERR_NOT_IMPLEMENTED;
}
