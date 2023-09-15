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

#define COLL_UCX_CREATE_COMMON(_op_name, _module, ... ) \
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

#define COLL_UCX_START_COMMON(_op_name, _ucg_req) \
    ret = ucg_collective_start(coll, _ucg_req); \
    if (OPAL_UNLIKELY(UCS_STATUS_IS_ERR(ret))) { \
        COLL_UCX_ERROR("ucx " #_op_name " start failed: %s", ucs_status_string(ret)); \
        return OMPI_ERROR; \
    }

#define COLL_UCX_COMPLETION(_ucg_req) \
    ucg_collective_destroy(coll); \
    if (ucs_unlikely((_ucg_req)->req_status.MPI_ERROR != OMPI_SUCCESS)) { \
        return OMPI_ERROR; \
    }

#define COLL_UCX_START_BLOCKING(_op_name, _module, ... ) \
    ompi_request_t ucg_req = {0}; \
    \
    COLL_UCX_CREATE_COMMON(_op_name, _module, __VA_ARGS__) \
    COLL_UCX_START_COMMON(_op_name, &ucg_req) \
    \
    if (ucs_likely(ret == UCS_OK)) { \
        ucg_collective_destroy(coll); \
        return OMPI_SUCCESS; \
    } \
    \
    MCA_COMMON_UCX_WAIT_LOOP(&ucg_req.req_complete, \
                             OPAL_COMMON_UCX_REQUEST_TYPE_UCG, coll, \
                             opal_common_ucx.ucp_worker, "ucx " #_op_name, \
                             COLL_UCX_COMPLETION(&ucg_req));


#define COLL_UCX_START_NONBLOCKING(_op_name, _module, ... ) \
    mca_common_ucx_persistent_request_t *req = \
      (mca_common_ucx_persistent_request_t *) \
            COMMON_UCX_FREELIST_GET(&ompi_common_ucx.requests); \
    \
    COLL_UCX_CREATE_COMMON(_op_name, _module, __VA_ARGS__) \
    COLL_UCX_START_COMMON(_op_name, &req->ompi) \
    \
    if (ucs_likely(ret == UCS_OK)) { \
        COMMON_UCX_FREELIST_RETURN(&ompi_common_ucx.requests, &req->ompi.super); \
        COLL_UCX_VERBOSE(8, "returning completed request"); \
        *request = &ompi_common_ucx.completed_request; \
        return OMPI_SUCCESS; \
    } else if (ucs_likely(ret == UCS_INPROGRESS)) { \
        COLL_UCX_VERBOSE(8, "started request %p", (void*)req); \
        req->ompi.req_mpi_object.comm = comm; \
        *request                      = &req->ompi; \
        return OMPI_SUCCESS; \
    } else { \
        COLL_UCX_ERROR("ucx collective failed: %s", ucs_status_string(ret)); \
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

#define COLL_UCX_TRACE(_msg, _sbuf, _rbuf, _count, _datatype, _comm, ...)        \
        COLL_UCX_VERBOSE(8, _msg " sbuf %p rbuf %p count %i type '%s' comm %d '%s'", \
                __VA_ARGS__, (_sbuf), (_rbuf), (_count), (_datatype)->name, \
              (_comm)->c_contextid, (_comm)->c_name);

static ompi_request_t*
mca_coll_ucx_persistent_start(mca_common_ucx_persistent_request_t *preq)
{
    mca_coll_ucx_persistent_request_t *coll_preq =
          (mca_coll_ucx_persistent_request_t*)preq;

    ucs_status_t status = ucg_collective_start(coll_preq->pcoll, preq);

    if (ucs_unlikely(status != UCS_OK)) {
        return (ompi_request_t*)status;
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
 * In the blocking version, request is placed on the stack, awaiting completion.
 * For non-blocking, request is allocated by UCX, awaiting completion.
 * For persistent requests, the collective starts later - only then the
 * (internal) request is created (by UCX) and placed as "tmp_req" inside
 * the persistent (external) request structure.
 */

int mca_coll_ucx_allgather(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, int rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(allgather, module, sbuf, scount, sdtype, rbuf, rcount,
                            rdtype, 0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_allgatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps,  struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_allreduce(const void *sbuf, void *rbuf, int count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(allreduce, module, sbuf, rbuf, count,
                            dtype, op, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_allreduce_stable(const void *sbuf, void *rbuf, int count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(allreduce, module, sbuf, rbuf, count,
                            dtype, op, 0 /* root */,
                            UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_alltoall(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void* rbuf, int rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(alltoall, module, sbuf, scount, sdtype, rbuf, rcount,
                            rdtype, 0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_alltoallv(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_alltoallw(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_barrier(struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(barrier, module, 0 /* ign */)
}

int mca_coll_ucx_bcast(void *buff, int count, struct ompi_datatype_t *datatype, int root,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(bcast, module, buff, buff, count, datatype,
                            0 /* op */, root, 0 /* modifiers */)
}

int mca_coll_ucx_exscan(const void *sbuf, void *rbuf, int count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_gather(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, int rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(gather, module, sbuf, scount, sdtype, rbuf, rcount,
                            rdtype, 0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_gatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *disps, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(gatherv, module, sbuf, scount, sdtype, rbuf, rcounts,
                            disps, rdtype, 0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_reduce(const void *sbuf, void* rbuf, int count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(reduce, module, sbuf, rbuf, count,
                            dtype, op, root, 0 /* modifiers */)
}

int mca_coll_ucx_reduce_stable(const void *sbuf, void* rbuf, int count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(reduce, module, sbuf, rbuf, count,
                            dtype, op, root,
                            UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_reduce_scatter(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_reduce_scatter_stable(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_reduce_scatter_block(const void *sbuf, void *rbuf, int rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_reduce_scatter_block_stable(const void *sbuf, void *rbuf, int rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_scan(const void *sbuf, void *rbuf, int count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_scatter(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, int rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
      COLL_UCX_START_BLOCKING(scatter, module, sbuf, scount, sdtype, rbuf, rcount,
                              rdtype, 0 /* op */, root, 0 /* modifiers */)
}

int mca_coll_ucx_scatterv(const void *sbuf, const int *scounts, const int *disps, struct ompi_datatype_t *sdtype,
   void* rbuf, int rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
      COLL_UCX_START_BLOCKING(scatterv, module, sbuf, scounts, disps, sdtype,
                              rbuf, rcount, rdtype, 0 /* op */, root, 0 /* modifiers */)
}

int mca_coll_ucx_iallgather(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, int rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(allgather, module, sbuf, scount, sdtype, rbuf, rcount,
                               rdtype, 0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_iallgatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps,  struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_iallreduce(const void *sbuf, void *rbuf, int count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm,
   ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(allreduce, module, sbuf, rbuf, count,
                               dtype, op, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_iallreduce_stable(const void *sbuf, void *rbuf, int count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm,
   ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(allreduce, module, sbuf, rbuf, count,
                               dtype, op, 0 /* root */,
                               UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE_STABLE)
}

int mca_coll_ucx_ialltoall(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void* rbuf, int rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(alltoall, module, sbuf, scount, sdtype, rbuf, rcount,
                               rdtype, 0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_ialltoallv(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_ialltoallw(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_ibarrier(struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_ibcast(void *buff, int count, struct ompi_datatype_t *datatype, int root,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(bcast, module, buff, buff, count, datatype,
                               0 /* op */, root, 0 /* modifiers */)

}

int mca_coll_ucx_iexscan(const void *sbuf, void *rbuf, int count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_igather(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, int rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(gather, module, sbuf, scount, sdtype, rbuf, rcount,
                               rdtype, 0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_igatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *disps, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(gatherv, module, sbuf, scount, sdtype, rbuf, rcounts,
                               disps, rdtype, 0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_ireduce(const void *sbuf, void* rbuf, int count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(reduce, module, sbuf, rbuf, count,
                               dtype, op, root, 0 /* modifiers */)
}

int mca_coll_ucx_ireduce_stable(const void *sbuf, void* rbuf, int count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(reduce, module, sbuf, rbuf, count,
                               dtype, op, root, 0 /* modifiers */)
}

int mca_coll_ucx_ireduce_scatter(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_ireduce_scatter_stable(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_ireduce_scatter_block(const void *sbuf, void *rbuf, int rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_ireduce_scatter_block_stable(const void *sbuf, void *rbuf, int rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_iscan(const void *sbuf, void *rbuf, int count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_iscatter(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, int rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
      COLL_UCX_START_NONBLOCKING(scatter, module, sbuf, scount, sdtype, rbuf, rcount,
                                 rdtype, 0 /* op */, root, 0 /* modifiers */)
}

int mca_coll_ucx_iscatterv(const void *sbuf, const int *scounts, const int *disps, struct ompi_datatype_t *sdtype,
   void* rbuf, int rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{
      COLL_UCX_START_NONBLOCKING(scatterv, module, sbuf, scounts, disps, sdtype,
                                 rbuf, rcount, rdtype, 0 /* op */, root, 0 /* modifiers */)
}

int mca_coll_ucx_allgather_init(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, int rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(allgather, module, sbuf, scount, sdtype, rbuf, rcount,
                              rdtype, 0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_allgatherv_init(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps,  struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_allreduce_init(const void *sbuf, void *rbuf, int count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info,
   ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(allreduce, module, sbuf, rbuf, count,
                              dtype, op, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_alltoall_init(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void* rbuf, int rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(alltoall, module, sbuf, scount, sdtype, rbuf, rcount,
                              rdtype, 0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_alltoallv_init(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_alltoallw_init(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_barrier_init(struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(barrier, module, 0 /* ign */)
}

int mca_coll_ucx_bcast_init(void *buff, int count, struct ompi_datatype_t *datatype, int root,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(bcast, module, buff, buff, count, datatype,
                              0 /* op */, root, 0 /* modifiers */)

}

int mca_coll_ucx_exscan_init(const void *sbuf, void *rbuf, int count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_gather_init(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, int rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(gather, module, sbuf, scount, sdtype, rbuf, rcount,
                              rdtype, 0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_gatherv_init(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *disps, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(gatherv, module, sbuf, scount, sdtype, rbuf, rcounts,
                              disps, rdtype, 0 /* op */, 0 /* root */, 0 /* modifiers */)
}

int mca_coll_ucx_reduce_init(const void *sbuf, void* rbuf, int count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(reduce, module, sbuf, rbuf, count,
                              dtype, op, root, 0 /* modifiers */)

}

int mca_coll_ucx_reduce_stable_init(const void *sbuf, void* rbuf, int count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(reduce, module, sbuf, rbuf, count,
                              dtype, op, root, 0 /* modifiers */)
}

int mca_coll_ucx_reduce_scatter_init(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_reduce_scatter_stable_init(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_reduce_scatter_block_init(const void *sbuf, void *rbuf, int rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_reduce_scatter_block_stable_init(const void *sbuf, void *rbuf, int rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_scan_init(const void *sbuf, void *rbuf, int count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_scatter_init(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, int rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
      COLL_UCX_START_PERSISTENT(scatter, module, sbuf, scount, sdtype, rbuf, rcount,
                                rdtype, 0 /* op */, root, 0 /* modifiers */)
}

int mca_coll_ucx_scatterv_init(const void *sbuf, const int *scounts, const int *disps, struct ompi_datatype_t *sdtype,
   void* rbuf, int rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
      COLL_UCX_START_PERSISTENT(scatterv, module, sbuf, scounts, disps, sdtype,
                                rbuf, rcount, rdtype, 0 /* op */, root, 0 /* modifiers */)
}

int mca_coll_ucx_neighbor_allgather(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, int rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_BLOCKING(allgather, module, sbuf, scount, sdtype, rbuf, rcount,
                            rdtype, 0 /* op */, 0 /* root */,
                            UCG_GROUP_COLLECTIVE_MODIFIER_NEIGHBOR)
}

int mca_coll_ucx_neighbor_allgatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps,  struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_neighbor_alltoall(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void* rbuf, int rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_neighbor_alltoallv(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_neighbor_alltoallw(const void *sbuf, const int *scounts, const MPI_Aint *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const MPI_Aint *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_ineighbor_allgather(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, int rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_NONBLOCKING(allgather, module, sbuf, scount, sdtype, rbuf, rcount,
                               rdtype, 0 /* op */, 0 /* root */,
                               UCG_GROUP_COLLECTIVE_MODIFIER_NEIGHBOR)
}

int mca_coll_ucx_ineighbor_allgatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps,  struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_ineighbor_alltoall(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void* rbuf, int rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_ineighbor_alltoallv(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_ineighbor_alltoallw(const void *sbuf, const int *scounts, const MPI_Aint *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const MPI_Aint *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_neighbor_allgather_init(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, int rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(allgather, module, sbuf, scount, sdtype, rbuf, rcount,
                              rdtype, 0 /* op */, 0 /* root */,
                              UCG_GROUP_COLLECTIVE_MODIFIER_NEIGHBOR)
}

int mca_coll_ucx_neighbor_allgatherv_init(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps,  struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_neighbor_alltoall_init(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void* rbuf, int rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_neighbor_alltoallv_init(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{

}

int mca_coll_ucx_neighbor_alltoallw_init(const void *sbuf, const int *scounts, const MPI_Aint *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const MPI_Aint *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module)
{
    COLL_UCX_START_PERSISTENT(alltoallw, module, sbuf, scounts, sdisps, rbuf, rcounts,
                              rdtypes, 0 /* op */, 0 /* root */,
                              UCG_GROUP_COLLECTIVE_MODIFIER_NEIGHBOR)
}
