/*
 * Copyright (c) 2020 Huawei Technologies Co., Ltd.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ompi_config.h"

#ifdef HAVE_UCG
#include <ucg/api/ucg.h>
#include <setjmp.h>
#endif

#include "common_ucx.h"

#include "ompi/communicator/communicator.h"
#include "ompi/mca/topo/base/base.h"
#include "ompi/op/op.h"

#define ESTIMATED_ENDPOINT_COUNT (10)

ompi_common_ucx_module_t ompi_common_ucx = {0};

static void mca_common_ucx_request_init_common(ompi_request_t* ompi_req,
                                               bool req_persistent,
                                               ompi_request_state_t state,
                                               ompi_request_free_fn_t req_free,
                                               ompi_request_cancel_fn_t req_cancel)
{
    OMPI_REQUEST_INIT(ompi_req, req_persistent);
    ompi_req->req_state            = state;
    ompi_req->req_start            = mca_common_ucx_start;
    ompi_req->req_free             = req_free;
    ompi_req->req_cancel           = req_cancel;
    /* This field is used to attach persistant request to a temporary req.
     * Receive (ucp_tag_recv_nb) may call completion callback
     * before the field is set. If the field is not NULL then mca_common_ucx_preq_completion()
     * will try to complete bogus persistant request.
     */
    ompi_req->req_complete_cb_data = NULL;
}

int mca_common_ucx_request_cancel(ompi_request_t *req, int flag)
{
    ucp_request_cancel(opal_common_ucx.ucp_worker, req);
    return OMPI_SUCCESS;
}

#if MPI_VERSION >= 4
int mca_pml_cancel_send_callback(struct ompi_request_t *request, int flag);
int mca_common_ucx_request_cancel_send(ompi_request_t *req, int flag)
{
    mca_pml_cancel_send_callback(req, flag);
    return mca_common_ucx_request_cancel(req, flag);
}
#endif

static void mca_common_ucx_request_init(void *request)
{
    ompi_request_t* ompi_req = request;
    OBJ_CONSTRUCT(ompi_req, ompi_request_t);
    mca_common_ucx_request_init_common(ompi_req, false, OMPI_REQUEST_ACTIVE,
                                       mca_common_ucx_request_free,
                                       mca_common_ucx_request_cancel);
}

static void mca_common_ucx_request_cleanup(void *request)
{
    ompi_request_t* ompi_req = request;
    ompi_req->req_state = OMPI_REQUEST_INVALID;
    OMPI_REQUEST_FINI(ompi_req);
    OBJ_DESTRUCT(ompi_req);
}

int mca_common_ucx_start(size_t count, ompi_request_t** requests)
{
    mca_common_ucx_persistent_request_t *preq;
    ompi_request_t *tmp_req;
    size_t i;

    for (i = 0; i < count; ++i) {
        preq = (mca_common_ucx_persistent_request_t *)requests[i];

        if ((preq == NULL) ||
            ((OMPI_REQUEST_PML  != preq->ompi.req_type) &&
             (OMPI_REQUEST_COLL != preq->ompi.req_type))) {
            /* Skip irrelevant requests */
            continue;
        }

        MCA_COMMON_UCX_ASSERT(preq->ompi.req_state != OMPI_REQUEST_INVALID);
        preq->ompi.req_state = OMPI_REQUEST_ACTIVE;
        mca_common_ucx_request_reset(&preq->ompi);

        tmp_req = preq->start_cb(preq);

        if (tmp_req == NULL) {
            MCA_COMMON_UCX_VERBOSE(8, "completed immediately, completing persistent request %p",
                                   (void*)preq);
            preq->ompi.req_status.MPI_ERROR = MPI_SUCCESS;
            ompi_request_complete(&preq->ompi, true);
        } else if (!UCS_PTR_IS_ERR(tmp_req)) {
            if (REQUEST_COMPLETE(tmp_req)) {
                /* tmp_req is already completed */
                MCA_COMMON_UCX_VERBOSE(8, "completing persistent request %p", (void*)preq);
                mca_common_ucx_persistent_request_complete(preq, tmp_req);
            } else {
                /* tmp_req would be completed by callback and trigger completion
                 * of preq */
                MCA_COMMON_UCX_VERBOSE(8, "temporary request %p will complete persistent request %p",
                                (void*)tmp_req, (void*)preq);
                tmp_req->req_complete_cb_data = preq;
                preq->tmp_req                 = tmp_req;
            }
        } else {
            MCA_COMMON_UCX_ERROR("request failed: %s",
                                 ucs_status_string(UCS_PTR_STATUS(tmp_req)));
            return OMPI_ERROR;
        }
    }

    return OMPI_SUCCESS;
}

static int mca_common_ucx_persistent_request_free(ompi_request_t **rptr)
{
    mca_common_ucx_persistent_request_t* preq = (mca_common_ucx_persistent_request_t*)*rptr;
    ompi_request_t *tmp_req = preq->tmp_req;

    preq->ompi.req_state = OMPI_REQUEST_INVALID;
    if (tmp_req != NULL) {
        mca_common_ucx_persistent_request_detach(preq, tmp_req);
        ucp_request_free(tmp_req);
    }
    COMMON_UCX_FREELIST_RETURN(&ompi_common_ucx.requests, &preq->ompi.super);
    *rptr = MPI_REQUEST_NULL;
    return OMPI_SUCCESS;
}

static int mca_common_ucx_persistent_request_cancel(ompi_request_t *req, int flag)
{
    mca_common_ucx_persistent_request_t* preq = (mca_common_ucx_persistent_request_t*)req;

    if (preq->tmp_req != NULL) {
        ucp_request_cancel(opal_common_ucx.ucp_worker, preq->tmp_req);
    }
    return OMPI_SUCCESS;
}

static void mca_common_ucx_persisternt_request_construct(mca_common_ucx_persistent_request_t* req)
{
    mca_common_ucx_request_init_common(&req->ompi, true, OMPI_REQUEST_INACTIVE,
                                       mca_common_ucx_persistent_request_free,
                                       mca_common_ucx_persistent_request_cancel);
    req->tmp_req = NULL;
}

static void mca_common_ucx_persisternt_request_destruct(mca_common_ucx_persistent_request_t* req)
{
    req->ompi.req_state = OMPI_REQUEST_INVALID;
    OMPI_REQUEST_FINI(&req->ompi);
}

OBJ_CLASS_INSTANCE(mca_common_ucx_persistent_request_t,
                   ompi_request_t,
                   mca_common_ucx_persisternt_request_construct,
                   mca_common_ucx_persisternt_request_destruct);

static int mca_common_completed_request_free(struct ompi_request_t** rptr)
{
    *rptr = MPI_REQUEST_NULL;
    return OMPI_SUCCESS;
}

static int mca_common_completed_request_cancel(struct ompi_request_t* ompi_req, int flag)
{
    return OMPI_SUCCESS;
}

static void mca_common_ucx_completed_request_init(ompi_request_t *ompi_req)
{
    mca_common_ucx_request_init_common(ompi_req, false, OMPI_REQUEST_ACTIVE,
                                       mca_common_completed_request_free,
                                       mca_common_completed_request_cancel);
    ompi_req->req_mpi_object.comm = &ompi_mpi_comm_null.comm;
    ompi_request_complete(ompi_req, false);
}

#ifdef HAVE_UCG
static int mca_common_ucx_datatype_convert(ompi_datatype_t *mpi_dt,
                                           ucp_datatype_t *ucp_dt)
{
    *ucp_dt = mca_common_ucx_get_datatype(mpi_dt);
    return 0;
}

static int mca_common_ucx_datatype_get_span(void *mpi_dt, int count,
                                            ptrdiff_t *span, ptrdiff_t *gap)
{
    *span = opal_datatype_span(mpi_dt, count, gap);
    return 0;
}

static int mca_common_ucx_is_dtype_int(ompi_datatype_t *dtype, int *is_signed)
{
    *is_signed = 0; /* Open MPI doesn't seem to have signedness information */
    return (dtype->super.flags & OMPI_DATATYPE_FLAG_DATA_INT);
}

static int mca_common_ucx_is_dtype_fp(ompi_datatype_t *dtype)
{
    return (dtype->super.flags & OMPI_DATATYPE_FLAG_DATA_FLOAT);
}

static int mca_common_ucx_get_operator(ompi_op_t *op,
                                       enum ucg_operator *operator,
                                       int *want_location, int *is_commutative)

{
    int location = 0;

    if (op == &ompi_mpi_op_maxloc.op) {
        location = 1;
        *operator = UCG_OPERATOR_MAX;
    } else if (op == &ompi_mpi_op_max.op) {
        *operator = UCG_OPERATOR_MAX;
    } else if (op == &ompi_mpi_op_minloc.op) {
        location = 1;
        *operator = UCG_OPERATOR_MIN;
    } else if (op == &ompi_mpi_op_min.op) {
        *operator = UCG_OPERATOR_MIN;
    } else if (op == &ompi_mpi_op_sum.op) {
        *operator = UCG_OPERATOR_SUM;
    } else if (op == &ompi_mpi_op_prod.op) {
        *operator = UCG_OPERATOR_PROD;
    } else if (op == &ompi_mpi_op_land.op) {
        *operator = UCG_OPERATOR_LAND;
    } else if (op == &ompi_mpi_op_band.op) {
        *operator = UCG_OPERATOR_BAND;
    } else if (op == &ompi_mpi_op_lor.op) {
        *operator = UCG_OPERATOR_LOR;
    } else if (op == &ompi_mpi_op_bor.op) {
        *operator = UCG_OPERATOR_BOR;
    } else if (op == &ompi_mpi_op_lxor.op) {
        *operator = UCG_OPERATOR_LXOR;
    } else if (op == &ompi_mpi_op_bxor.op) {
        *operator = UCG_OPERATOR_BXOR;
    } else if (op == &ompi_mpi_op_no_op.op) {
        *operator = UCG_OPERATOR_NOP;
    } else {
        return -1;
    }

    *is_commutative = ompi_op_is_commute(op);
    *want_location  = location;
    return 0;
}

static int mca_common_ucx_resolve_address(ompi_communicator_t *comm,
                                          ucg_group_member_index_t rank,
                                          ucp_address_t **addr,
                                          size_t *addr_len)
{
    /* Sanity checks */
    if (rank == (ucg_group_member_index_t)comm->c_my_rank) {
        MCA_COMMON_UCX_ERROR("mca_common_ucx_resolve_address(rank=%u)"
                             "shouldn't be called on its own rank (loopback)", rank);
        return 1;
    }

    /* Check the cache for a previously established connection to that rank */
    ompi_proc_t *proc_peer = ompi_comm_peer_lookup(comm, rank);
    *addr = proc_peer->proc_endpoints[OMPI_PROC_ENDPOINT_TAG_UCX];
    *addr_len = 0; /* UCX doesn't need the length to unpack the address */
    if (*addr) {
       return 0;
    }

    /* Obtain the UCP address of the remote */
    int ret = opal_common_ucx_recv_worker_address(&proc_peer->super.proc_name,
                                                  addr, addr_len);
    if (ret < 0) {
        MCA_COMMON_UCX_ERROR("mca_common_ucx_recv_worker_address(proc=%d rank=%u) failed",
                             proc_peer->super.proc_name.vpid, rank);
        return 1;
    }


    MCA_COMMON_UCX_VERBOSE(2, "Got process #%d address, size %ld",
                           proc_peer->super.proc_name.vpid, *addr_len);

    /* Cache the connection for future invocations with this rank */
    proc_peer->proc_endpoints[OMPI_PROC_ENDPOINT_TAG_UCX] = *addr;
    return 0;
}

static void mca_common_ucx_release_address(ucp_address_t *addr)
{
    /* no need to free - the address is stored in proc_peer->proc_endpoints */
}

static int mca_common_ucx_neighbors_count(ompi_communicator_t *comm,
                                          int *indegree, int *outdegree)
{
    if (OMPI_COMM_IS_CART(comm)) {
        /* cartesian */
        /* outdegree is always 2*ndims because we need to iterate
         * over empty buffers for MPI_PROC_NULL */
        *outdegree = *indegree = 2 * comm->c_topo->mtc.cart->ndims;
    } else if (OMPI_COMM_IS_GRAPH(comm)) {
        /* graph */
        int nneighbors, rank = ompi_comm_rank(comm);
        mca_topo_base_graph_neighbors_count(comm, rank, &nneighbors);
        *outdegree = *indegree = nneighbors;
    } else if (OMPI_COMM_IS_DIST_GRAPH(comm)) {
        /* graph */
        *indegree = comm->c_topo->mtc.dist_graph->indegree;
        *outdegree = comm->c_topo->mtc.dist_graph->outdegree;
    } else {
        return 1;
    }

    return 0;
}

static int mca_common_ucx_neighbors_query(ompi_communicator_t *comm,
                                          ucg_group_member_index_t *sources,
                                          ucg_group_member_index_t *destinations)
{
    int res, indeg, outdeg;

    res = mca_common_ucx_neighbors_count(comm, &indeg, &outdeg);
    if (!res) {
        return res;
    }

    if (OMPI_COMM_IS_CART(comm)) {
        /* cartesian */
        int rpeer, speer;

        /* silence clang static analyzer warning */
        assert(indeg == outdeg);

        for (int dim = 0, i = 0; dim < comm->c_topo->mtc.cart->ndims; ++dim) {
            mca_topo_base_cart_shift(comm, dim, 1, &rpeer, &speer);
            sources[i] = destinations[i] = (ucg_group_member_index_t)rpeer;
            i++;
            sources[i] = destinations[i] = (ucg_group_member_index_t)speer;
            i++;
        }
    } else if (OMPI_COMM_IS_GRAPH(comm)) {
        /* graph */
        mca_topo_base_graph_neighbors(comm, ompi_comm_rank(comm), indeg, sources);
        memcpy(destinations, sources, indeg * sizeof (int));
    } else if (OMPI_COMM_IS_DIST_GRAPH(comm)) {
        /* dist graph */
        mca_topo_base_dist_graph_neighbors(comm, indeg, sources,
                MPI_UNWEIGHTED, outdeg, destinations, MPI_UNWEIGHTED);
    }

    return 0;
}

static inline void
mca_common_ucx_set_imbalance(ompi_communicator_t *comm,  double imbalance_time)
{
    comm->c_imbalance = imbalance_time;
}

static __opal_attribute_always_inline__ void
mca_common_ucx_async_complete(ompi_request_t *request, bool is_success,
                              ucs_status_t status, bool is_req_persistent)
{
    mca_common_ucx_persistent_request_t *preq;

    mca_common_ucx_set_status(&request->req_status, status);
    ompi_request_complete(request, true);

    assert(is_success == (status == UCS_OK));
    assert(is_req_persistent == request->req_persistent);

    if (is_req_persistent) {
        preq = (mca_common_ucx_persistent_request_t*)request->req_complete_cb_data;
        if (preq != NULL) {
            MCA_COMMON_UCX_ASSERT(preq->tmp_req != NULL);
            mca_common_ucx_persistent_request_complete(preq, request);
        }
    }
}

jmp_buf mca_common_ucx_blocking_collective_call;
volatile bool is_mca_common_ucx_blocking_collective_call_armed;

static __opal_attribute_always_inline__ void
mca_common_ucx_imm_complete(void *req, bool is_success, ucs_status_t status,
                            bool is_req_persistent)
{
#if OPAL_ENABLE_PROGRESS_THREADS == 0
    assert(is_req_persistent == false);
    assert(is_success == (status == UCS_OK));
    if (ucs_likely(is_mca_common_ucx_blocking_collective_call_armed)) {
        longjmp(mca_common_ucx_blocking_collective_call,
                (is_success ? OPAL_SUCCESS : OPAL_ERROR) - 1);
    }
#endif
}

static void mca_common_ucx_imm_success(void *req, ucs_status_t status)
{
    mca_common_ucx_imm_complete(req, true, status, false);
}

static void mca_common_ucx_imm_success_persistent(void *req, ucs_status_t status)
{
    mca_common_ucx_imm_complete(req, true, status, true);
}

static void mca_common_ucx_imm_failure(void *req, ucs_status_t status)
{
    mca_common_ucx_imm_complete(req, false, status, false);
}

static void mca_common_ucx_imm_failure_persistent(void *req, ucs_status_t status)
{
    mca_common_ucx_imm_complete(req, false, status, true);
}

static void mca_common_ucx_async_success(void *req, ucs_status_t status)
{
    mca_common_ucx_async_complete(req, true, status, false);
}

static void mca_common_ucx_async_success_persistent(void *req, ucs_status_t status)
{
    mca_common_ucx_async_complete(req, true, status, true);
}

static void mca_common_ucx_async_failure(void *req, ucs_status_t status)
{
    mca_common_ucx_async_complete(req, false, status, false);
}

static void mca_common_ucx_async_failure_persistent(void *req, ucs_status_t status)
{
    mca_common_ucx_async_complete(req, false, status, true);
}

static int mca_coll_ucx_get_global_rank(ompi_communicator_t *comm,
                                        ucg_group_member_index_t comm_rank,
                                        ucg_group_member_index_t *global_rank_p)
{
    struct ompi_proc_t *proc = (struct ompi_proc_t*)ompi_comm_peer_lookup(comm, comm_rank);

    *global_rank_p = (ucg_group_member_index_t)proc->super.proc_name.vpid;

    return 0;
}

/* See prte_rmaps_base_print_mapping() for the string values */
static char *distance_lookup[] = {
        "none",
        "hwthread",
        "core",
        "l1cache",
        "l2cache",
        "l3cache",
        "package",
        "numa",
        "board",
        "node",
        NULL
};

static enum ucg_group_member_distance
mca_common_ucx_get_pmix_distance(const char *pmix_key)
{
    int ret;
    char *pmix_value;

    opal_process_name_t wildcard = {
            OMPI_PROC_MY_NAME->jobid,
            OPAL_VPID_WILDCARD
    };

    OPAL_MODEX_RECV_VALUE_OPTIONAL(ret, pmix_key, &wildcard,
                                   &pmix_value, PMIX_STRING);

    if (ret) {
        MCA_COMMON_UCX_WARN("Failed to obtain a value from PMIx: %s (error #%i)", pmix_key, ret);
        return UCG_GROUP_MEMBER_DISTANCE_UNKNOWN;
    }

    MCA_COMMON_UCX_VERBOSE(8, "PMIx translated key \"%s\" to value \"%s\"",
                           pmix_key, pmix_value);

    if (0 == strncasecmp(pmix_value, "by", 2)) {
        pmix_value += 2;
    }

    enum ucg_group_member_distance iterator;
    for (iterator = 0; distance_lookup[iterator]; iterator++) {
        if (0 == strncasecmp(pmix_value, distance_lookup[iterator],
                             strlen(distance_lookup[iterator]))) {
            MCA_COMMON_UCX_VERBOSE(8, "UCX translated \"%s\" to distance %u",
                                   pmix_key, iterator);
            return iterator;
        }
    }

    MCA_COMMON_UCX_VERBOSE(8, "UCX was unable to translate \"%s\"", pmix_key);

    return UCG_GROUP_MEMBER_DISTANCE_UNKNOWN;
}

static enum ucg_group_member_distance mca_common_ucx_get_map_by(void)
{
    return mca_common_ucx_get_pmix_distance(PMIX_MAPBY);
}

static enum ucg_group_member_distance mca_common_ucx_get_rank_by(void)
{
    return mca_common_ucx_get_pmix_distance(PMIX_RANKBY);
}

static enum ucg_group_member_distance mca_common_ucx_get_bind_to(void)
{
    return mca_common_ucx_get_pmix_distance(PMIX_BINDTO);
}
#endif

int mca_common_ucx_open(const char *prefix, size_t *request_size)
{
    ucp_params_t ucp_params;

    /* Initialize UCX context */
    ucp_params.field_mask        = UCP_PARAM_FIELD_FEATURES |
                                   UCP_PARAM_FIELD_REQUEST_SIZE |
                                   UCP_PARAM_FIELD_REQUEST_INIT |
                                   UCP_PARAM_FIELD_REQUEST_CLEANUP |
                                   UCP_PARAM_FIELD_TAG_SENDER_MASK |
                                   UCP_PARAM_FIELD_MT_WORKERS_SHARED |
                                   UCP_PARAM_FIELD_ESTIMATED_NUM_EPS;
    ucp_params.features          = UCP_FEATURE_TAG |
                                   /* The features below are for SPML-UCX */
                                   UCP_FEATURE_RMA   |
                                   UCP_FEATURE_AMO32 |
                                   UCP_FEATURE_AMO64;
    ucp_params.request_size      = sizeof(ompi_request_t);
    ucp_params.request_init      = mca_common_ucx_request_init;
    ucp_params.request_cleanup   = mca_common_ucx_request_cleanup;
    ucp_params.tag_sender_mask   = MCA_COMMON_UCX_SPECIFIC_SOURCE_MASK;
    ucp_params.mt_workers_shared = 0; /* we do not need mt support for context
                                         since it will be protected by worker */
    ucp_params.estimated_num_eps = ESTIMATED_ENDPOINT_COUNT;

#ifdef HAVE_DECL_UCP_PARAM_FIELD_ESTIMATED_NUM_PPN
    ucp_params.estimated_num_ppn = opal_process_info.num_local_peers + 1;
    ucp_params.field_mask       |= UCP_PARAM_FIELD_ESTIMATED_NUM_PPN;
#endif

#ifdef HAVE_DECL_UCP_FLAG_LOCAL_TLS_ONLY
    if (ompi_process_info.num_local_peers == ompi_process_info.num_procs) {
        ucp_params.field_mask   |= UCP_PARAM_FIELD_FLAGS;
        ucp_params.flags         = UCP_FLAG_LOCAL_TLS_ONLY;
    }
#endif

#ifdef HAVE_UCG
    ucb_params_t ucb_params;
    ucg_params_t ucg_params;

    /* Initialize UCG context parameters */
    ucp_params.features                |= UCP_FEATURE_GROUPS;
    ucb_params.super                    = &ucp_params;
    ucb_params.field_mask               = 0;
    ucg_params.super                    = &ucb_params;
    ucg_params.field_mask               = UCG_PARAM_FIELD_NAME             |
                                          UCG_PARAM_FIELD_ADDRESS_CB       |
                                          UCG_PARAM_FIELD_NEIGHBORS_CB     |
                                          UCG_PARAM_FIELD_DATATYPE_CB      |
                                          UCG_PARAM_FIELD_REDUCE_OP_CB     |
                                          UCG_PARAM_FIELD_COMPLETION_CB    |
                                          UCG_PARAM_FIELD_SET_IMBALANCE_CB |
                                          UCG_PARAM_FIELD_MPI_IN_PLACE     |
                                          UCG_PARAM_FIELD_HANDLE_FAULT     |
                                          UCG_PARAM_FIELD_JOB_INFO         |
                                          UCG_PARAM_FIELD_GLOBAL_INDEX;
                                          // UCG_GROUP_PARAM_FIELD_CACHE_SIZE
    // ucg_params.cache_size = 5;
    ucg_params.name                     = "Open MPI context";
    ucg_params.address.lookup_f         = (typeof(ucg_params.address.lookup_f))
                                          mca_common_ucx_resolve_address;
    ucg_params.address.release_f        = (typeof(ucg_params.address.release_f))
                                          mca_common_ucx_release_address;
    ucg_params.neighbors.vertex_count_f = (typeof(ucg_params.neighbors.vertex_count_f))
                                          mca_common_ucx_neighbors_count;
    ucg_params.neighbors.vertex_query_f = (typeof(ucg_params.neighbors.vertex_query_f))
                                          mca_common_ucx_neighbors_query;
    ucg_params.datatype.convert_f       = (typeof(ucg_params.datatype.convert_f))
                                          mca_common_ucx_datatype_convert;
    ucg_params.datatype.get_span_f      = (typeof(ucg_params.datatype.get_span_f))
                                          mca_common_ucx_datatype_get_span;
    ucg_params.datatype.is_integer_f    = (typeof(ucg_params.datatype.is_integer_f))
                                          mca_common_ucx_is_dtype_int;
    ucg_params.datatype.is_floating_point_f = (typeof(ucg_params.datatype.is_floating_point_f))
                                              mca_common_ucx_is_dtype_fp;
    ucg_params.reduce_op.reduce_cb_f    = (typeof(ucg_params.reduce_op.reduce_cb_f))
                                          ompi_op_reduce;
    ucg_params.reduce_op.get_operator_f = (typeof(ucg_params.reduce_op.get_operator_f))
                                          mca_common_ucx_get_operator;
    ucg_params.completion.comp_cb_f[0][0][0] = (ucg_collective_comp_cb_t)
                                               mca_common_ucx_imm_success;
    ucg_params.completion.comp_cb_f[0][0][1] = (ucg_collective_comp_cb_t)
                                               mca_common_ucx_imm_success_persistent;
    ucg_params.completion.comp_cb_f[0][1][0] = (ucg_collective_comp_cb_t)
                                               mca_common_ucx_imm_failure;
    ucg_params.completion.comp_cb_f[0][1][1] = (ucg_collective_comp_cb_t)
                                               mca_common_ucx_imm_failure_persistent;
    ucg_params.completion.comp_cb_f[1][0][0] = (ucg_collective_comp_cb_t)
                                               mca_common_ucx_async_success;
    ucg_params.completion.comp_cb_f[1][0][1] = (ucg_collective_comp_cb_t)
                                               mca_common_ucx_async_success_persistent;
    ucg_params.completion.comp_cb_f[1][1][0] = (ucg_collective_comp_cb_t)
                                               mca_common_ucx_async_failure;
    ucg_params.completion.comp_cb_f[1][1][1] = (ucg_collective_comp_cb_t)
                                               mca_common_ucx_async_failure_persistent;
    ucg_params.set_imbalance_cb_f       = (ucg_collective_imbalance_set_cb_t)
                                          mca_common_ucx_set_imbalance;
    ucg_params.mpi_in_place             = (void*)MPI_IN_PLACE;
    ucg_params.get_global_index_f       = (typeof(ucg_params.get_global_index_f))
                                          mca_coll_ucx_get_global_rank;
    ucg_params.fault.mode               = UCG_FAULT_IS_FATAL;
    ucg_params.job_info.job_uid         = ompi_process_info.my_name.jobid;
    ucg_params.job_info.step_idx        = 0; /* TODO: support steps, e.g. in SLURM*/
    ucg_params.job_info.job_size        = ompi_process_info.num_procs;
    ucg_params.job_info.map_by          = mca_common_ucx_get_map_by();
    ucg_params.job_info.rank_by         = mca_common_ucx_get_rank_by();
    ucg_params.job_info.bind_to         = mca_common_ucx_get_bind_to();
    ucg_params.job_info.procs_per_host  = opal_process_info.num_local_peers + 1;

    return opal_common_ucx_open(prefix, &ucg_params,
#else
    return opal_common_ucx_open(prefix, &ucp_params,
#endif
                                request_size);
}

int mca_common_ucx_close(void)
{
    return opal_common_ucx_close();
}

void mca_common_ucx_enable(void)
{
    if (ompi_common_ucx.is_initialized) {
        return;
    }
    ompi_common_ucx.is_initialized = 1;

    OBJ_CONSTRUCT(&ompi_common_ucx.requests, mca_common_ucx_freelist_t);

    COMMON_UCX_FREELIST_INIT(&ompi_common_ucx.requests,
                             mca_common_ucx_persistent_request_t,
                             MCA_COMMON_UCX_PERSISTENT_REQUEST_SLACK,
                             128, -1, 128);

    OBJ_CONSTRUCT(&ompi_common_ucx.completed_request, ompi_request_t);
    mca_common_ucx_completed_request_init(&ompi_common_ucx.completed_request);
#if MPI_VERSION >= 4
    ompi_common_ucx.completed_request.req_cancel = mca_pml_cancel_send_callback;
#endif

    OBJ_CONSTRUCT(&ompi_common_ucx.datatype_ctx, mca_common_ucx_datatype_ctx_t);

}

static void mca_common_ucx_common_finalize(void)
{
    if (!ompi_common_ucx.is_initialized) {
        return;
    }
    ompi_common_ucx.is_initialized = 0;

    OBJ_DESTRUCT(&ompi_common_ucx.datatype_ctx);

    ompi_common_ucx.completed_request.req_state = OMPI_REQUEST_INVALID;
    OMPI_REQUEST_FINI(&ompi_common_ucx.completed_request);
    OBJ_DESTRUCT(&ompi_common_ucx.completed_request);

    OBJ_DESTRUCT(&ompi_common_ucx.requests);
}

static ucs_thread_mode_t mca_common_ucx_thread_mode(int ompi_mode)
{
    switch (ompi_mode) {
    case MPI_THREAD_MULTIPLE:
        return UCS_THREAD_MODE_MULTI;
    case MPI_THREAD_SERIALIZED:
        return UCS_THREAD_MODE_SERIALIZED;
    case MPI_THREAD_FUNNELED:
    case MPI_THREAD_SINGLE:
    default:
        return UCS_THREAD_MODE_SINGLE;
    }
}

int mca_common_ucx_init(const mca_base_component_t *version)
{
    ucs_thread_mode_t mode = opal_common_ucx_thread_mode(ompi_mpi_thread_provided);

    return opal_common_ucx_init(mode, version);
}

int mca_common_ucx_cleanup(void)
{
    mca_common_ucx_common_finalize();

    return opal_common_ucx_cleanup();
}
