/*
 * Copyright (C) 2001 Mellanox Technologies Ltd.  ALL RIGHTS RESERVED.
 * Copyright (c) 2016 The University of Tennessee and The University of
 *                    Tennessee Research Foundation.  All rights reserved.
 * Copyright (c) 2019 Huawei Technologies Co., Ltd. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */


#include "coll_ucx.h"
#include "coll_ucx_request.h"

#include "ompi/message/message.h"
#include <inttypes.h>

#define COLL_UCX_START_COMMON(_op_name, _module, ... ) \
    mca_coll_ucx_module_t *ucx_module = (mca_coll_ucx_module_t*)(_module); \
\
    ucg_coll_h coll; \
    ucs_status_t ret = ucg_coll_##_op_name##_init(__VA_ARGS__, \
                                                  ucx_module->ucg_group, \
                                                  &coll); \
    if (OPAL_UNLIKELY(UCS_STATUS_IS_ERR(ret))) { \
        COLL_UCX_ERROR("ucx " #_op_name " init failed: %s", ucs_status_string(ret)); \
        return OMPI_ERROR; \
    } \
\
    ret = ucg_collective_start(coll, &ucg_req); \
    if (OPAL_UNLIKELY(UCS_STATUS_IS_ERR(ret))) { \
        COLL_UCX_ERROR("ucx " #_op_name " start failed: %s", ucs_status_string(ret)); \
        return OMPI_ERROR; \
    } \
\
    if (ucs_unlikely(ret == UCS_OK)) { \
        return OMPI_SUCCESS; \
    } \
\

#define COLL_UCX_START_BLOCKING(_op_name, _module, ... ) \
    ucg_request_t ucg_req; \
    COLL_UCX_START_COMMON(_op_name, _module, __VA_ARGS__) \
    MCA_COMMON_UCX_WAIT_LOOP(&ucg_req, OPAL_COMMON_UCX_REQUEST_TYPE_UCG, \
                             ucx_module->ucg_group, "ucx " #_op_name, \
                             ucg_collective_destroy(coll));

#define COLL_UCX_START_NONBLOCKING(_op_name, _module, ... ) \
    ucg_request_t ucg_req; /* TODO: allocate a request on the heap, unlike this or COLL_UCX_REQ_ALLOCA */ \
    COLL_UCX_START_COMMON(_op_name, _module, __VA_ARGS__)

int mca_coll_ucx_start(size_t count, ompi_request_t** requests)
{
    mca_coll_ucx_persistent_op_t *preq;
    ompi_request_t *tmp_req;
    size_t i;

    for (i = 0; i < count; ++i) {
        preq = (mca_coll_ucx_persistent_op_t *)requests[i];
        if ((preq == NULL) || (OMPI_REQUEST_COLL != preq->ompi.req_type)) {
            /* Skip irrelevant requests */
            continue;
        }

        COLL_UCX_ASSERT(preq->ompi.req_state != OMPI_REQUEST_INVALID);
        preq->ompi.req_state = OMPI_REQUEST_ACTIVE;
        mca_coll_ucx_request_reset(&preq->ompi);

        tmp_req = NULL; // TODO: ucg_collective_start(preq->coll_desc, ???);
        if (tmp_req == NULL) {
            COLL_UCX_VERBOSE(8, "collective completed immediately, completing persistent request %p",
                    (void*)preq);
            mca_coll_ucx_set_coll_status(&preq->ompi.req_status, UCS_OK);
            ompi_request_complete(&preq->ompi, true);
        } else if (!UCS_PTR_IS_ERR(tmp_req)) {
            if (REQUEST_COMPLETE(tmp_req)) {
                /* tmp_req is already completed */
                COLL_UCX_VERBOSE(8, "completing persistent request %p", (void*)preq);
                mca_coll_ucx_persistent_op_complete(preq, tmp_req);
            } else {
                /* tmp_req would be completed by callback and trigger completion
                 * of preq */
                COLL_UCX_VERBOSE(8, "temporary request %p will complete persistent request %p",
                        (void*)tmp_req, (void*)preq);
                tmp_req->req_complete_cb_data = preq;
                preq->tmp_req                 = tmp_req;
            }
        } else {
            COLL_UCX_ERROR("ucx collective failed: %s", ucs_status_string(UCS_PTR_STATUS(tmp_req)));
            return OMPI_ERROR;
        }
    }

    return OMPI_SUCCESS;
}

/*
 * For each type of collectives there are 3 varieties of function calls:
 * blocking, non-blocking and persistent initialization. For example, for
 * the allreduce collective operations, those would be called:
 * - mca_coll_ucx_allreduce
 * - mca_coll_ucx_iallreduce
 * - mca_coll_ucx_iallreduce_init
 *
 * In the blocking version, request is placed on the stack, awaiting completion.
 * For non-blocking, request is allocated by UCX, awaiting completion.
 * For persistent requests, the collective starts later - only then the
 * (internal) request is created (by UCX) and placed as "tmp_req" inside
 * the persistent (external) request structure.
 */

#define COLL_UCX_TRACE(_msg, _sbuf, _rbuf, _count, _datatype, _comm, ...)        \
        COLL_UCX_VERBOSE(8, _msg " sbuf %p rbuf %p count %i type '%s' comm %d '%s'", \
                __VA_ARGS__, (_sbuf), (_rbuf), (_count), (_datatype)->name, \
                (_comm)->c_contextid, (_comm)->c_name);

#define COLL_UCX_REQ_ALLOCA(ucx_module) \
        ((char *)alloca(mca_coll_ucx_component.request_size) + \
                mca_coll_ucx_component.request_size);

int mca_coll_ucx_barrier(struct ompi_communicator_t *comm,
        mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(barrier, module, 0 /* cb */)
}

static inline int __opal_attribute_always_inline__
mca_coll_ucx_handle_dtype(struct ompi_datatype_t *dtype, int count, size_t *dtype_size)
{
    ptrdiff_t lb;
    ompi_datatype_get_true_extent(dtype, &lb, (ptrdiff_t*)dtype_size);
    return (lb == 0) &&
            opal_datatype_is_contiguous_memory_layout(&dtype->super, count);
}

int mca_coll_ucx_bcast(void *buff, int count, struct ompi_datatype_t *dtype,
        int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    size_t dtype_size;
    if (ucs_unlikely(!mca_coll_ucx_handle_dtype(dtype, count, &dtype_size))) {
        return OMPI_ERR_NOT_SUPPORTED;
    }

    COLL_UCX_START_BLOCKING(bcast, module, buff, buff, count, dtype_size, dtype,
            0 /* cb */, 0 /* op */, root, 0 /* modifiers */,
            root == comm->c_my_rank /* am I the root */)
}

int mca_coll_ucx_reduce(const void *sbuf, void* rbuf, int count,
                        struct ompi_datatype_t *dtype, struct ompi_op_t *op,
                        int root, struct ompi_communicator_t *comm,
                        mca_coll_base_module_t *module)
{
    size_t dtype_size;
    if (ucs_unlikely(!mca_coll_ucx_handle_dtype(dtype, count, &dtype_size))) {
        return OMPI_ERR_NOT_SUPPORTED;
    }

    COLL_UCX_START_BLOCKING(reduce, module, sbuf, rbuf, count, dtype_size,
            dtype, 0 /* cb */, op, root, 0 /* modifiers */,
            root == comm->c_my_rank /* am I the root */)
    // TODO: potentially pass a "stable reduction" modifier
}

int mca_coll_ucx_allreduce(const void *sbuf, void *rbuf, int count,
                           struct ompi_datatype_t *dtype, struct ompi_op_t *op,
                           struct ompi_communicator_t *comm,
                           mca_coll_base_module_t *module)
{
    size_t dtype_size;
    if (ucs_unlikely(!mca_coll_ucx_handle_dtype(dtype, count, &dtype_size))) {
        return OMPI_ERR_NOT_SUPPORTED;
    }

    COLL_UCX_START_BLOCKING(allreduce, module, sbuf, rbuf, count, dtype_size,
            dtype, 0 /* cb */, op, 0 /* root */, 0 /* modifiers */)
    // TODO: potentially pass a "stable reduction" modifier
}


/* This is just a placeholder - not working yet....
int mca_coll_ucx_iallreduce(const void *sbuf, void *rbuf, int count,
                            struct ompi_datatype_t *dtype, struct ompi_op_t *op,
                            struct ompi_communicator_t *comm,
                            struct ompi_request_t **request,
                            mca_coll_base_module_t *module)
{
    size_t dtype_size;
    if (ucs_unlikely(!mca_coll_ucx_handle_dtype(dtype, count, &dtype_size))) {
        return OMPI_ERR_NOT_SUPPORTED;
    }

    COLL_UCX_START_NONBLOCKING(allreduce, module, sbuf, rbuf, count, dtype_size,
                               dtype, 0, op, 0, 0)

    *request = ucg_req; // TODO: handle the case where it completed immediately
    return OMPI_SUCCESS;
}

int mca_coll_ucx_allreduce_init(const void *sbuf, void *rbuf, int count,
                                struct ompi_datatype_t *dtype, struct ompi_op_t *op,
                                struct ompi_communicator_t *comm,
                                struct ompi_info_t *info,
                                struct ompi_request_t **request,
                                mca_coll_base_module_t *module)
{
    mca_coll_ucx_module_t *ucx_module = (mca_coll_ucx_module_t*)module;

    mca_coll_ucx_persistent_op_t *req =
            (mca_coll_ucx_persistent_op_t *)
            COLL_UCX_FREELIST_GET(&mca_coll_ucx_component.persistent_ops);
    if (OPAL_UNLIKELY(req == NULL)) {
        return OMPI_ERR_OUT_OF_RESOURCE;
    }

    COLL_UCX_TRACE("iallreduce_init request *%p=%p",
            sbuf, rbuf, count, dtype, comm, (void*)request, (void*)req);

    int dtype_size;
    MPI_Type_size(dtype, &dtype_size);
    ucs_status_t ret = ucg_coll_allreduce_init(sbuf, rbuf, count,
            dtype_size, dtype, mca_coll_ucx_pcoll_completion, op, 0,
            UCG_GROUP_COLLECTIVE_MODIFIER_PERSISTENT, ucx_module->ucg_group,
            &req->coll_desc);
    if (OPAL_UNLIKELY(ret != UCS_OK)) {
        COLL_UCX_ERROR("ucx allreduce failed: %s", ucs_status_string(ret));
        return OMPI_ERROR;
    }

    req->ompi.req_state = OMPI_REQUEST_INACTIVE;
    *request = &req->ompi;
    return OMPI_SUCCESS;
}
*/

int mca_coll_ucx_scatter(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
                               void *rbuf, int rcount, struct ompi_datatype_t *rdtype,
                         int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    size_t sdtype_size, rdtype_size;
    if (ucs_unlikely(!mca_coll_ucx_handle_dtype(sdtype, scount, &sdtype_size))) {
        return OMPI_ERR_NOT_SUPPORTED;
    }
    if (ucs_unlikely(!mca_coll_ucx_handle_dtype(rdtype, rcount, &rdtype_size))) {
        return OMPI_ERR_NOT_SUPPORTED;
    }

    COLL_UCX_START_BLOCKING(scatter, module, sbuf, scount, sdtype_size, sdtype,
            rbuf, rcount, rdtype_size, rdtype, 0 /* cb */, 0 /* op */, root,
            0 /* modifiers */, root == comm->c_my_rank /* am I the root */)
}

int mca_coll_ucx_scatterv(const void *sbuf, const int *scounts, const int *sdispls, struct ompi_datatype_t *sdtype,
                                void *rbuf,       int  rcount,                      struct ompi_datatype_t *rdtype,
                          int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    size_t sdtype_size, rdtype_size;
    if (ucs_unlikely(!mca_coll_ucx_handle_dtype(sdtype, 2, &sdtype_size))) {
        return OMPI_ERR_NOT_SUPPORTED;
    }
    if (ucs_unlikely(!mca_coll_ucx_handle_dtype(rdtype, rcount, &rdtype_size))) {
        return OMPI_ERR_NOT_SUPPORTED;
    }

    COLL_UCX_START_BLOCKING(scatterv, module, sbuf, scounts, sdispls,
            sdtype_size, sdtype, rbuf, rcount, rdtype_size, rdtype, 0 /* cb */,
            0 /* op */, root, 0 /* modifiers */,
            root == comm->c_my_rank /* am I the root */)
}

int mca_coll_ucx_gather(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
                              void *rbuf, int rcount, struct ompi_datatype_t *rdtype,
                        int root, struct ompi_communicator_t *comm,
                        mca_coll_base_module_t *module)
{
    size_t sdtype_size, rdtype_size;
    if (ucs_unlikely(!mca_coll_ucx_handle_dtype(sdtype, scount, &sdtype_size))) {
        return OMPI_ERR_NOT_SUPPORTED;
    }
    if (ucs_unlikely(!mca_coll_ucx_handle_dtype(rdtype, rcount, &rdtype_size))) {
        return OMPI_ERR_NOT_SUPPORTED;
    }

    COLL_UCX_START_BLOCKING(gather, module, sbuf, scount, sdtype_size, sdtype,
            rbuf, rcount, rdtype_size, rdtype, 0 /* cb */, 0 /* op */,
            0 /* root */, 0 /* modifiers */, root == comm->c_my_rank /* am I the root */)
}

int mca_coll_ucx_gatherv(const void *sbuf,       int  scount,                      struct ompi_datatype_t *sdtype,
                               void *rbuf, const int *rcounts, const int *rdispls, struct ompi_datatype_t *rdtype,
                         int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    size_t sdtype_size, rdtype_size;
    if (ucs_unlikely(!mca_coll_ucx_handle_dtype(sdtype, scount, &sdtype_size))) {
        return OMPI_ERR_NOT_SUPPORTED;
    }
    if (ucs_unlikely(!mca_coll_ucx_handle_dtype(rdtype, 2, &rdtype_size))) {
        return OMPI_ERR_NOT_SUPPORTED;
    }

    COLL_UCX_START_BLOCKING(gatherv, module, sbuf, scount, sdtype_size, sdtype,
            rbuf, rcounts, rdispls, rdtype_size, rdtype, 0 /* cb */, 0 /* op */,
            0 /* root */, 0 /* modifiers */, root == comm->c_my_rank /* am I the root */)
}

int mca_coll_ucx_allgather(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
                                 void *rbuf, int rcount, struct ompi_datatype_t *rdtype,
                           struct ompi_communicator_t *comm,
                           mca_coll_base_module_t *module)
{
    size_t sdtype_size, rdtype_size;
    if (ucs_unlikely(!mca_coll_ucx_handle_dtype(sdtype, scount, &sdtype_size))) {
        return OMPI_ERR_NOT_SUPPORTED;
    }
    if (ucs_unlikely(!mca_coll_ucx_handle_dtype(rdtype, rcount, &rdtype_size))) {
        return OMPI_ERR_NOT_SUPPORTED;
    }

    COLL_UCX_START_BLOCKING(allgather, module, sbuf, scount, sdtype_size,
            sdtype, rbuf, rcount, rdtype_size, rdtype, 0 /* cb */, 0 /* op */,
            0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_alltoall(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
                                void *rbuf, int rcount, struct ompi_datatype_t *rdtype,
                          struct ompi_communicator_t *comm,
                          mca_coll_base_module_t *module)
{
    size_t sdtype_size, rdtype_size;
    if (ucs_unlikely(!mca_coll_ucx_handle_dtype(sdtype, scount, &sdtype_size))) {
        return OMPI_ERR_NOT_SUPPORTED;
    }
    if (ucs_unlikely(!mca_coll_ucx_handle_dtype(rdtype, rcount, &rdtype_size))) {
        return OMPI_ERR_NOT_SUPPORTED;
    }

    COLL_UCX_START_BLOCKING(alltoall, module, sbuf, scount, sdtype_size, sdtype,
            rbuf, rcount, rdtype_size, rdtype, 0 /* cb */, 0 /* op */,
            0 /* root */, 0 /* modifiers */)
}
