/*
 * Copyright (C) 2001 Mellanox Technologies Ltd.  ALL RIGHTS RESERVED.
 * Copyright (c) 2016 The University of Tennessee and The University of
 *                    Tennessee Research Foundation.  All rights reserved.
 * Copyright (c) 2020 Huawei Technologies Co., Ltd. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "coll_ucc.h"

int mca_coll_ucc_barrier(struct ompi_communicator_t *comm,
                         mca_coll_base_module_t *module)
{
    int ret;
    ucc_coll_req_h ucc_request;
    ucc_coll_op_args_t coll_args;
    mca_coll_ucc_module_t *ucc_module;

    ucc_module          = (mca_coll_ucc_module_t*)module;
    coll_args.mask      = UCC_COLL_ARG_FIELD_COLL_TYPE;
    coll_args.coll_type = UCC_COLL_TYPE_BARRIER;

    ret = ucc_collective_init_and_post(&coll_args, &ucc_request,
                                       ucc_module->ucc_team);

    if (ucs_likely(ret == UCC_OK)) {
        return OMPI_SUCCESS;
    }

    MCA_COMMON_UCX_WAIT_LOOP(ucc_request,
                             OPAL_COMMON_UCX_REQUEST_TYPE_UCC_COLL, 0,
                             opal_common_ucx.ucp_worker, "ucc barrier",
                             ucc_collective_finalize(ucc_request));
}

int mca_coll_ucc_bcast(void *buff, int count, struct ompi_datatype_t *dtype,
                       int root, struct ompi_communicator_t *comm,
                       mca_coll_base_module_t *module)
{
    return OMPI_ERR_NOT_IMPLEMENTED;
}

int mca_coll_ucc_reduce(const void *sbuf, void* rbuf, int count,
                        struct ompi_datatype_t *dtype, struct ompi_op_t *op,
                        int root, struct ompi_communicator_t *comm,
                        mca_coll_base_module_t *module)
{
    return OMPI_ERR_NOT_IMPLEMENTED;
}

int mca_coll_ucc_allreduce(const void *sbuf, void *rbuf, int count,
                           struct ompi_datatype_t *dtype, struct ompi_op_t *op,
                           struct ompi_communicator_t *comm,
                           mca_coll_base_module_t *module)
{
    return OMPI_ERR_NOT_IMPLEMENTED;
}
