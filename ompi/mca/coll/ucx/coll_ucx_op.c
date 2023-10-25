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

#include "ompi/message/message.h"
#include <inttypes.h>
#include <setjmp.h>

static ompi_request_t blocking_ucg_req = {0};

#define COLL_UCX_CREATE_COMMON(_op_name, _module, ... ) \
    mca_coll_ucx_module_t *ucx_module = (mca_coll_ucx_module_t*)(_module); \
    \
    ucg_coll_h coll; \
    UCS_V_UNUSED ucg_collective_progress_t progress; \
    ucs_status_t init_status = ucg_coll_##_op_name##_init(__VA_ARGS__, \
                                                          ucx_module->ucg_group, \
                                                          &coll); \
    if (OPAL_UNLIKELY(init_status != UCS_OK)) { \
        COLL_UCX_ERROR("ucx " #_op_name " init failed: %s", \
                       ucs_status_string(init_status)); \
        return OMPI_ERROR; \
    } \

#define COLL_UCX_REQEUST_PENDING(_ucg_req) \
    (_ucg_req)->req_complete = REQUEST_PENDING;

#if OPAL_ENABLE_PROGRESS_THREADS == 0
extern jmp_buf mca_common_ucx_blocking_collective_call;
extern volatile bool is_mca_common_ucx_blocking_collective_call_armed;
#define COLL_UCX_START_PREPARE_SHORTCUT(_ucg_req, _is_send_only) \
    int ompi_status; \
    if (_is_send_only) { \
        is_mca_common_ucx_blocking_collective_call_armed = false; \
        COLL_UCX_REQEUST_PENDING(_ucg_req) \
    } else { \
        is_mca_common_ucx_blocking_collective_call_armed = true; \
        ompi_status = setjmp(mca_common_ucx_blocking_collective_call); \
        if (ompi_status) { \
            return ompi_status + 1; \
        } \
    }
#define COLL_UCX_START_FOLLOWUP_SHORTCUT(_ucg_req) \
   COLL_UCX_REQEUST_PENDING(_ucg_req)
#else
#define COLL_UCX_START_PREPARE_SHORTCUT(_ucg_req, _is_send_only) \
   COLL_UCX_REQEUST_PENDING(_ucg_req)
#define COLL_UCX_START_FOLLOWUP_SHORTCUT(_ucg_req) \
   UCS_V_UNUSED int ompi_status;
#endif

#define COLL_UCX_START_COMMON(_op_name, _ucg_req, _is_send_only) \
    COLL_UCX_START_PREPARE_SHORTCUT(_ucg_req, _is_send_only) \
    ucs_status_ptr_t ret = ucg_collective_start(coll, (_ucg_req), &progress);

#define COLL_UCX_START_BLOCKING(_op_name, _module, _is_send_only, ... ) \
    COLL_UCX_CREATE_COMMON(_op_name, _module, __VA_ARGS__) \
    COLL_UCX_START_COMMON(_op_name, &blocking_ucg_req, _is_send_only) \
    \
    if (ucs_likely(!UCS_PTR_IS_PTR(ret))) { \
        if (ucs_likely(UCS_PTR_RAW_STATUS(ret)) == UCS_OK) { \
            return OMPI_SUCCESS; \
         } else { \
             COLL_UCX_ERROR("ucx " #_op_name " start failed: %s", \
                            ucs_status_string(UCS_PTR_STATUS(ret))); \
             return OMPI_ERROR; \
         } \
    } \
    COLL_UCX_START_FOLLOWUP_SHORTCUT(&blocking_ucg_req) \
    MCA_COMMON_UCX_WAIT_LOOP(ret, OPAL_COMMON_UCX_REQUEST_TYPE_UCG, \
                             opal_common_ucx.ucp_worker, "blocking " #_op_name, \
                             progress, ompi_status = 0); \
    return OMPI_ERROR; /* should never happen */

#define COLL_UCX_START_NONBLOCKING(_op_name, _module, _is_send_only, ... ) \
    mca_common_ucx_persistent_request_t *req = \
      (mca_common_ucx_persistent_request_t *) \
            COMMON_UCX_FREELIST_GET(&ompi_common_ucx.requests); \
    \
    COLL_UCX_CREATE_COMMON(_op_name, _module, __VA_ARGS__) \
    COLL_UCX_START_COMMON(_op_name, &req->ompi, _is_send_only) \
    \
    if (ucs_likely(UCS_PTR_STATUS(ret) == UCS_OK)) { \
        COMMON_UCX_FREELIST_RETURN(&ompi_common_ucx.requests, &req->ompi.super); \
        COLL_UCX_VERBOSE(8, "returning completed request"); \
        *request = &ompi_common_ucx.completed_request; \
        return OMPI_SUCCESS; \
    } else if (ucs_likely(UCS_PTR_STATUS(ret) == UCS_INPROGRESS)) { \
        COLL_UCX_VERBOSE(8, "started request %p", (void*)req); \
        req->ompi.req_mpi_object.comm = comm; \
        *request                      = &req->ompi; \
        return OMPI_SUCCESS; \
    } else { \
        COLL_UCX_ERROR("ucx collective failed: %s", \
                       ucs_status_string(UCS_PTR_STATUS(ret))); \
        return OMPI_ERROR; \
    }

#define COLL_UCX_START_PERSISTENT(_op_name, _module, ... ) \
    mca_coll_ucx_persistent_request_t *req; \
    \
    COLL_UCX_CREATE_COMMON(_op_name, _module, __VA_ARGS__) \
    \
    int rc = mca_common_ucx_persistent_request_init(OMPI_REQUEST_COLL, comm, \
                                                    mca_coll_ucx_persistent_start, \
                                                    (mca_common_ucx_persistent_request_t**)&req); \
    if (rc != OMPI_SUCCESS) { \
        return rc; \
    } \
    \
    req->pcoll = coll; \
    *request = &req->super.ompi; \
    return OMPI_SUCCESS;

// TODO: call ucg_collective_destroy(coll) only for persistent collectives

#define COLL_UCX_TRACE(_msg, _sbuf, _rbuf, _count, _datatype, _comm, ...)        \
        COLL_UCX_VERBOSE(8, _msg " sbuf %p rbuf %p count %i type '%s' comm %d '%s'", \
                __VA_ARGS__, (_sbuf), (_rbuf), (_count), (_datatype)->name, \
              (_comm)->c_contextid, (_comm)->c_name);

static ompi_request_t*
mca_coll_ucx_persistent_start(mca_common_ucx_persistent_request_t *preq)
{
    mca_coll_ucx_persistent_request_t *coll_preq =
          (mca_coll_ucx_persistent_request_t*)preq;

    ucs_status_ptr_t ret = ucg_collective_start(coll_preq->pcoll, preq,
                                                &coll_preq->progress_f);

    if (ucs_unlikely(UCS_PTR_IS_ERR(ret) != UCS_OK)) {
        return (ompi_request_t*)UCS_PTR_RAW_STATUS(ret);
    }

    return &preq->ompi;
}

/*
 * For each type of collectives there are 3 varieties of function calls:
 * blocking, non-blocking and persistent initialization. For example, for
 * the allreduce collective operations, those would be called:
 * - mca_coll_ucx_allreduce
 * - mca_coll_ucx_iallreduce
 * - mca_coll_ucx_iallreduce_init
 *
 * Specifically for collectives including reduction, these stable-reduction
 * alternatives will also be available:
 * - mca_coll_ucx_allreduce_stable
 * - mca_coll_ucx_iallreduce_stable
 * - mca_coll_ucx_iallreduce_stable_init
 *
 * In the blocking version, request is placed on the stack, awaiting completion.
 * For non-blocking, request is allocated by UCX, awaiting completion.
 * For persistent requests, the collective starts later - only then the
 * (internal) request is created (by UCX) and placed as "tmp_req" inside
 * the persistent (external) request structure.
 */

int mca_coll_ucx_allgather(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(allgather, module, 0, sbuf, scount, sdtype, rbuf, rcount,
                            rdtype, 0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_allgatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(allgatherv, module, 0,
                            sbuf, scount, sdtype,
                            rbuf, rcounts, disps, rdtype,
                            0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_allreduce(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(allreduce, module, 0, sbuf, rbuf, count,
                            dtype, op, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_allreduce_stable(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(allreduce, module, 0, sbuf, rbuf, count,
                            dtype, op, 0 /* root */,
                            UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_alltoall(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void* rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(alltoall, module, 0, sbuf, scount, sdtype, rbuf, rcount,
                            rdtype, 0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_alltoallv(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(alltoallv, module, 0,
                            sbuf, scounts, sdisps, sdtype,
                            rbuf, rcounts, rdisps, rdtype,
                            0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_alltoallw(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(alltoallw, module, 0,
                            sbuf, scounts, sdisps, (void * const *) sdtypes,
                            rbuf, rcounts, rdisps, (void * const *) rdtypes,
                            0 /* op */, 0 /* root */, 0 /* modifiers */)
}

static inline int
ucg_coll_group_barrier_init(mca_coll_ucx_module_t *coll_ucx_module,
                            ucg_group_h ucg_group,
                            ucg_coll_h *coll_p)
{
    if ((*coll_p = coll_ucx_module->barrier_init) == NULL) {
         COLL_UCX_CREATE_COMMON(barrier, coll_ucx_module,
                                UCG_GROUP_COLLECTIVE_MODIFIER_PERSISTENT);
         *coll_p = coll_ucx_module->barrier_init = coll;
    }

    return OMPI_SUCCESS;
}

int mca_coll_ucx_barrier(struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    mca_coll_ucx_module_t *coll_ucx_module = (mca_coll_ucx_module_t*)module;
    COLL_UCX_START_BLOCKING(group_barrier, module, 0, coll_ucx_module)
}

int mca_coll_ucx_bcast(void *buff, size_t count, struct ompi_datatype_t *datatype, int root,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(bcast, module, root == comm->c_my_rank, buff, buff,
                            count, datatype, 0 /* op */, root, 0 /* modifiers */)
}

int mca_coll_ucx_exscan(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(exscan, module, 0, sbuf, rbuf, count, dtype,
                            op, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_exscan_stable(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(exscan, module, 0, sbuf, rbuf, count, dtype,
                            op, 0 /* root */,
                            UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_gather(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(gather, module, root != comm->c_my_rank, sbuf, scount,
                            sdtype, rbuf, rcount, rdtype, 0 /* op */, 0 /* root */,
                            0 /* modifiers */)
}

int mca_coll_ucx_gatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *disps, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(gatherv, module, root != comm->c_my_rank, sbuf, scount,
                            sdtype, rbuf, rcounts, disps, rdtype, 0 /* op */,
                            0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_reduce(const void *sbuf, void* rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(reduce, module, root != comm->c_my_rank, sbuf, rbuf,
                            count, dtype, op, root, 0 /* modifiers */)
}

int mca_coll_ucx_reduce_stable(const void *sbuf, void* rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(reduce, module, root != comm->c_my_rank, sbuf, rbuf,
                            count, dtype, op, root,
                            UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_reduce_scatter(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(reduce_scatter, module, 0,
                            sbuf, ompi_comm_rank(comm),
                            rbuf, rcounts, dtype,
                            op, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_reduce_scatter_stable(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(reduce_scatter, module, 0,
                            sbuf, ompi_comm_rank(comm),
                            rbuf, rcounts, dtype,
                            op, 0 /* root */,
                            UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_reduce_scatter_block(const void *sbuf, void *rbuf, size_t rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(reduce_scatter_block, module, 0, sbuf, rbuf, rcount,
                            dtype, op, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_reduce_scatter_block_stable(const void *sbuf, void *rbuf, size_t rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(reduce_scatter_block, module, 0, sbuf, rbuf, rcount,
                            dtype, op, 0 /* root */,
                            UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_scan(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(scan, module, 0, sbuf, rbuf, count, dtype,
                            op, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_scan_stable(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(scan, module, 0, sbuf, rbuf, count, dtype,
                            op, 0 /* root */,
                            UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_scatter(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
      COLL_UCX_START_BLOCKING(scatter, module, root == comm->c_my_rank, sbuf,
                              scount, sdtype, rbuf, rcount, rdtype, 0 /* op */,
                              root, 0 /* modifiers */)
}

int mca_coll_ucx_scatterv(const void *sbuf, const int *scounts, const int *disps, struct ompi_datatype_t *sdtype,
   void* rbuf, int rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
      COLL_UCX_START_BLOCKING(scatterv, module, root == comm->c_my_rank, sbuf,
                              scounts, disps, sdtype, rbuf, rcount, rdtype,
                              0 /* op */, root, 0 /* modifiers */)
}

int mca_coll_ucx_iallgather(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(allgather, module, 0, sbuf, scount, sdtype, rbuf,
                               rcount, rdtype, 0 /* op */, 0 /* root */,
                               0 /* modifiers */)
}

int mca_coll_ucx_iallgatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(allgatherv, module, 0,
                               sbuf, scount, sdtype,
                               rbuf, rcounts, disps, rdtype,
                               0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_iallreduce(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm,
   ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(allreduce, module, 0, sbuf, rbuf, count,
                               dtype, op, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_iallreduce_stable(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm,
   ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(allreduce, module, 0, sbuf, rbuf, count,
                               dtype, op, 0 /* root */,
                               UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_ialltoall(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void* rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(alltoall, module, 0, sbuf, scount, sdtype, rbuf,
                               rcount, rdtype, 0 /* op */, 0 /* root */,
                               0 /* modifiers */)
}

int mca_coll_ucx_ialltoallv(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(alltoallv, module, 0,
                               sbuf, scounts, sdisps, sdtype,
                               rbuf, rcounts, rdisps, rdtype,
                               0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_ialltoallw(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(alltoallw, module, 0,
                               sbuf, scounts, sdisps, (void * const *) sdtypes,
                               rbuf, rcounts, rdisps, (void * const *) rdtypes,
                               0 /* op */, 0 /* root */, 0 /* modifiers */)
}

static inline int
ucg_coll_group_ibarrier_init(mca_coll_ucx_module_t *coll_ucx_module,
                             ucg_group_h ucg_group,
                             ucg_coll_h *coll_p)
{
    if ((*coll_p = coll_ucx_module->ibarrier_init) == NULL) {
         COLL_UCX_CREATE_COMMON(ibarrier, coll_ucx_module,
                                UCG_GROUP_COLLECTIVE_MODIFIER_PERSISTENT);
         *coll_p = coll_ucx_module->ibarrier_init = coll;
    }

    return OMPI_SUCCESS;
}

int mca_coll_ucx_ibarrier(struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    mca_coll_ucx_module_t *coll_ucx_module = (mca_coll_ucx_module_t*)module;
    COLL_UCX_START_NONBLOCKING(group_ibarrier, module, 0, coll_ucx_module)
}

int mca_coll_ucx_ibcast(void *buff, size_t count, struct ompi_datatype_t *datatype, int root,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(bcast, module, root == comm->c_my_rank, buff,
                               buff, count, datatype, 0 /* op */, root,
                               0 /* modifiers */)

}

int mca_coll_ucx_iexscan(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(exscan, module, 0, sbuf, rbuf, count, dtype,
                               op, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_iexscan_stable(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(exscan, module, 0, sbuf, rbuf, count, dtype, op, 0 /* root */,
                               UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_igather(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(gather, module, root != comm->c_my_rank, sbuf,
                               scount, sdtype, rbuf, rcount, rdtype, 0 /* op */,
                               0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_igatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *disps, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(gatherv, module, root != comm->c_my_rank, sbuf,
                               scount, sdtype, rbuf, rcounts, disps, rdtype,
                               0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_ireduce(const void *sbuf, void* rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(reduce, module, root != comm->c_my_rank, sbuf,
                               rbuf, count, dtype, op, root, 0 /* modifiers */)
}

int mca_coll_ucx_ireduce_stable(const void *sbuf, void* rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(reduce, module, root != comm->c_my_rank, sbuf,
                               rbuf, count, dtype, op, root, 0 /* modifiers */)
}

int mca_coll_ucx_ireduce_scatter(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(reduce_scatter, module, 0,
                               sbuf, ompi_comm_rank(comm),
                               rbuf, rcounts, dtype,
                               op, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_ireduce_scatter_stable(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(reduce_scatter, module, 0,
                               sbuf, ompi_comm_rank(comm),
                               rbuf, rcounts, dtype,
                               op, 0 /* root */,
                               UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_ireduce_scatter_block(const void *sbuf, void *rbuf, size_t rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(reduce_scatter_block, module, 0, sbuf, rbuf, rcount,
                            dtype, op, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_ireduce_scatter_block_stable(const void *sbuf, void *rbuf, size_t rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(reduce_scatter_block, module, 0, sbuf, rbuf, rcount,
                            dtype, op, 0 /* root */,
                            UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_iscan(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(scan, module, 0, sbuf, rbuf, count, dtype,
                               op, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_iscan_stable(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(scan, module, 0, sbuf, rbuf, count, dtype,
                               op, 0 /* root */,
                               UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_iscatter(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
      COLL_UCX_START_NONBLOCKING(scatter, module, root == comm->c_my_rank, sbuf,
                                 scount, sdtype, rbuf, rcount, rdtype, 0 /* op */,
                                 root, 0 /* modifiers */)
}

int mca_coll_ucx_iscatterv(const void *sbuf, const int *scounts, const int *disps, struct ompi_datatype_t *sdtype,
   void* rbuf, int rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
      COLL_UCX_START_NONBLOCKING(scatterv, module, root == comm->c_my_rank, sbuf,
                                 scounts, disps, sdtype, rbuf, rcount, rdtype,
                                 0 /* op */, root, 0 /* modifiers */)
}

int mca_coll_ucx_allgather_init(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(allgather, module, sbuf, scount, sdtype, rbuf, rcount,
                              rdtype, 0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_allgatherv_init(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(allgatherv, module,
                              sbuf, scount, sdtype,
                              rbuf, rcounts, disps, rdtype,
                              0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_allreduce_init(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info,
   ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(allreduce, module, sbuf, rbuf, count,
                              dtype, op, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_allreduce_stable_init(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info,
   ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(allreduce, module, sbuf, rbuf, count,
                              dtype, op, 0 /* root */,
                              UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_alltoall_init(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void* rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(alltoall, module, sbuf, scount, sdtype, rbuf, rcount,
                              rdtype, 0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_alltoallv_init(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(alltoallv, module,
                              sbuf, scounts, sdisps, sdtype,
                              rbuf, rcounts, rdisps, rdtype,
                              0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_alltoallw_init(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(alltoallw, module,
                              sbuf, scounts, sdisps, (void * const *) sdtypes,
                              rbuf, rcounts, rdisps, (void * const *) rdtypes,
                              0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_barrier_init(struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(barrier, module, 0 /* ign */)
}

int mca_coll_ucx_bcast_init(void *buff, size_t count, struct ompi_datatype_t *datatype, int root,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(bcast, module, buff, buff, count, datatype,
                              0 /* op */, root, 0 /* modifiers */)

}

int mca_coll_ucx_exscan_init(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(exscan, module, sbuf, rbuf, count, dtype, op,
                              0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_exscan_stable_init(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(exscan, module, sbuf, rbuf, count, dtype, op, 0 /* root */,
                              UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_gather_init(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(gather, module,
                              sbuf, scount, sdtype,
                              rbuf, rcount, rdtype,
                              0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_gatherv_init(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *disps, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(gatherv, module,
                              sbuf, scount, sdtype,
                              rbuf, rcounts, disps, rdtype,
                              0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_reduce_init(const void *sbuf, void* rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(reduce, module, sbuf, rbuf, count, dtype, op,
                              root, 0 /* modifiers */)

}

int mca_coll_ucx_reduce_stable_init(const void *sbuf, void* rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(reduce, module, sbuf, rbuf, count, dtype, op, root,
                              UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_reduce_scatter_init(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(reduce_scatter, module,
                              sbuf, ompi_comm_rank(comm),
                              rbuf, rcounts, dtype,
                              op, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_reduce_scatter_stable_init(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(reduce_scatter, module,
                              sbuf, ompi_comm_rank(comm),
                              rbuf, rcounts, dtype,
                              op, 0 /* root */,
                              UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_reduce_scatter_block_init(const void *sbuf, void *rbuf, size_t rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(reduce_scatter_block, module, sbuf, rbuf, rcount,
                              dtype, op, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_reduce_scatter_block_stable_init(const void *sbuf, void *rbuf, size_t rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(reduce_scatter_block, module, sbuf, rbuf, rcount,
                              dtype, op, 0 /* root */,
                              UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_scan_init(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(scan, module, sbuf, rbuf, count, dtype, op,
                              0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_scan_stable_init(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(scan, module, sbuf, rbuf, count, dtype, op, 0 /* root */,
                              UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_scatter_init(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(scatter, module,
                              sbuf, scount, sdtype,
                              rbuf, rcount, rdtype,
                              0 /* op */, root, 0 /* modifiers */)
}

int mca_coll_ucx_scatterv_init(const void *sbuf, const int *scounts, const int *disps, struct ompi_datatype_t *sdtype,
   void* rbuf, int rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(scatterv, module,
                              sbuf, scounts, disps,
                              sdtype, rbuf, rcount, rdtype,
                              0 /* op */, root, 0 /* modifiers */)
}

int mca_coll_ucx_neighbor_allgather(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(neighbor_allgather, module, 0,
                            sbuf, scount, sdtype,
                            rbuf, rcount, rdtype,
                            0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_neighbor_allgatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(neighbor_allgatherv, module, 0,
                            sbuf, scount, sdtype,
                            rbuf, rcounts, disps, rdtype,
                            0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_neighbor_alltoall(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void* rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(neighbor_alltoall, module, 0,
                            sbuf, scount, sdtype,
                            rbuf, rcount, rdtype,
                            0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_neighbor_alltoallv(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(neighbor_alltoallv, module, 0,
                            sbuf, scounts, sdisps, sdtype,
                            rbuf, rcounts, rdisps, rdtype,
                            0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_neighbor_alltoallw(const void *sbuf, const int *scounts, const MPI_Aint *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const MPI_Aint *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(neighbor_alltoallw, module, 0,
                            sbuf, scounts, sdisps, (void * const *) sdtypes,
                            rbuf, rcounts, rdisps, (void * const *) rdtypes,
                            0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_ineighbor_allgather(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(neighbor_allgather, module, 0,
                               sbuf, scount, sdtype,
                               rbuf, rcount, rdtype,
                               0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_ineighbor_allgatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(neighbor_allgatherv, module, 0,
                               sbuf, scount, sdtype,
                               rbuf, rcounts, disps, rdtype,
                               0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_ineighbor_alltoall(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void* rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(neighbor_alltoall, module, 0,
                               sbuf, scount, sdtype,
                               rbuf, rcount, rdtype,
                               0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_ineighbor_alltoallv(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(neighbor_alltoallv, module, 0,
                               sbuf, scounts, sdisps, sdtype,
                               rbuf, rcounts, rdisps, rdtype,
                               0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_ineighbor_alltoallw(const void *sbuf, const int *scounts, const MPI_Aint *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const MPI_Aint *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(neighbor_alltoallw, module, 0,
                               sbuf, scounts, sdisps, (void * const *) sdtypes,
                               rbuf, rcounts, rdisps, (void * const *) rdtypes,
                               0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_neighbor_allgather_init(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(neighbor_allgather, module,
                              sbuf, scount, sdtype,
                              rbuf, rcount, rdtype,
                              0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_neighbor_allgatherv_init(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(neighbor_allgatherv, module,
                              sbuf, scount, sdtype,
                              rbuf, rcounts, disps, rdtype,
                              0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_neighbor_alltoall_init(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void* rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(neighbor_alltoall, module,
                              sbuf, scount, sdtype,
                              rbuf, rcount, rdtype,
                              0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_neighbor_alltoallv_init(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(neighbor_alltoallv, module,
                              sbuf, scounts, sdisps, sdtype,
                              rbuf, rcounts, rdisps, rdtype,
                              0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_neighbor_alltoallw_init(const void *sbuf, const int *scounts, const MPI_Aint *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const MPI_Aint *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(neighbor_alltoallw, module,
                              sbuf, scounts, sdisps, (void * const *) sdtypes,
                              rbuf, rcounts, rdisps, (void * const *) rdtypes,
                              0 /* op */, 0 /* root */, 0 /* modifiers */)
}
