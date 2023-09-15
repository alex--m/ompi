/*
 * Copyright (c) 2011 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2014 Research Organization for Information Science and
 *                    Technology (RIST). All rights reserved.
 * Copyright (c) 2019 Huawei Technologies Co., Ltd. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ompi_config.h"
#include "ompi/constants.h"
#include "ompi/datatype/ompi_datatype.h"
#include "ompi/mca/coll/base/coll_base_functions.h"
#include "ompi/op/op.h"

#include "coll_ucx.h"

static int mca_coll_ucg_create(mca_coll_ucx_module_t *module,
                               struct ompi_communicator_t *comm)
{
    ucs_status_t error;
    ucg_group_params_t args;
    ucg_group_member_index_t rank_idx;
    ucg_context_h ucg_ctx = opal_common_ucx.ucg_context;

#if OMPI_GROUP_SPARSE
    COLL_UCX_ERROR("Sparse process groups are not supported");
    return UCS_ERR_UNSUPPORTED;
#endif

    /* Fill in group initialization parameters */
    args.field_mask               = UCG_GROUP_PARAM_FIELD_UCP_WORKER   |
                                    UCG_GROUP_PARAM_FIELD_MEMBER_COUNT |
                                    UCG_GROUP_PARAM_FIELD_MEMBER_INDEX |
                                    UCG_GROUP_PARAM_FIELD_CB_CONTEXT   |
                                    UCG_GROUP_PARAM_FIELD_DISTANCES    |
                                    UCG_GROUP_PARAM_FIELD_NAME         |
                                    UCG_GROUP_PARAM_FIELD_FLAGS        |
                                    UCG_GROUP_PARAM_FIELD_WIREUP_POOL;
    args.worker                   = opal_common_ucx.ucp_worker;
    args.member_count             = ompi_comm_size(comm);
    args.member_index             = ompi_comm_rank(comm);
    args.cb_context               = comm;
    args.name                     = comm->c_name;
    args.flags                    = 0;
    args.wireup_pool              = ucg_context_get_wireup_message_pool(ucg_ctx);
    args.distance_type            = UCG_GROUP_DISTANCE_TYPE_ARRAY;
    args.distance_array           = alloca(args.member_count *
                                           sizeof(*args.distance_array));
    if (args.distance_array == NULL) {
        COLL_UCX_ERROR("Failed to allocate memory for %u local ranks", args.member_count);
        return OMPI_ERROR;
    }

    if (mca_coll_ucx_component.get_imbalance) {
        args.flags |= UCG_GROUP_CREATE_FLAG_TX_TIMESTAMP;
    }

    /* Generate (temporary) rank-distance array */
    for (rank_idx = 0; rank_idx < args.member_count; rank_idx++) {
        struct ompi_proc_t *rank_iter =
                (struct ompi_proc_t*)ompi_comm_peer_lookup(comm, rank_idx);
        if (rank_idx == args.member_index) {
            args.distance_array[rank_idx] = UCG_GROUP_MEMBER_DISTANCE_NONE;
        } else if (OPAL_PROC_ON_LOCAL_HWTHREAD(rank_iter->super.proc_flags)) {
            args.distance_array[rank_idx] = UCG_GROUP_MEMBER_DISTANCE_HWTHREAD;
        } else if (OPAL_PROC_ON_LOCAL_CORE(rank_iter->super.proc_flags)) {
            args.distance_array[rank_idx] = UCG_GROUP_MEMBER_DISTANCE_CORE;
        } else if (OPAL_PROC_ON_LOCAL_L1CACHE(rank_iter->super.proc_flags)) {
            args.distance_array[rank_idx] = UCG_GROUP_MEMBER_DISTANCE_L1CACHE;
        } else if (OPAL_PROC_ON_LOCAL_L2CACHE(rank_iter->super.proc_flags)) {
            args.distance_array[rank_idx] = UCG_GROUP_MEMBER_DISTANCE_L2CACHE;
        } else if (OPAL_PROC_ON_LOCAL_L3CACHE(rank_iter->super.proc_flags)) {
            args.distance_array[rank_idx] = UCG_GROUP_MEMBER_DISTANCE_L3CACHE;
        } else if (OPAL_PROC_ON_LOCAL_SOCKET(rank_iter->super.proc_flags)) {
            args.distance_array[rank_idx] = UCG_GROUP_MEMBER_DISTANCE_SOCKET;
        } else if (OPAL_PROC_ON_LOCAL_NUMA(rank_iter->super.proc_flags)) {
            args.distance_array[rank_idx] = UCG_GROUP_MEMBER_DISTANCE_NUMA;
        } else if (OPAL_PROC_ON_LOCAL_BOARD(rank_iter->super.proc_flags)) {
            args.distance_array[rank_idx] = UCG_GROUP_MEMBER_DISTANCE_BOARD;
        } else if (OPAL_PROC_ON_LOCAL_HOST(rank_iter->super.proc_flags)) {
            args.distance_array[rank_idx] = UCG_GROUP_MEMBER_DISTANCE_HOST;
        } else if (OPAL_PROC_ON_LOCAL_CU(rank_iter->super.proc_flags)) {
            args.distance_array[rank_idx] = UCG_GROUP_MEMBER_DISTANCE_CU;
        } else if (OPAL_PROC_ON_LOCAL_CLUSTER(rank_iter->super.proc_flags)) {
            args.distance_array[rank_idx] = UCG_GROUP_MEMBER_DISTANCE_CLUSTER;
        } else {
            args.distance_array[rank_idx] = UCG_GROUP_MEMBER_DISTANCE_UNKNOWN;
        }
    }

    if (OMPI_COMM_IS_CART(comm)  ||
        OMPI_COMM_IS_GRAPH(comm) ||
        OMPI_COMM_IS_DIST_GRAPH(comm)) {
        args.flags |= UCG_GROUP_CREATE_FLAG_NEIGHBORHOOD;
    }

    /* TODO: add support for comm->c_remote_comm  */
    /* TODO: add support for sparse group storage */

    /* Examine comm_new return value */
    error = ucg_group_create(&args, &module->ucg_group);
    if (error != UCS_OK) {
        COLL_UCX_ERROR("ucg_group_create failed: %s", ucs_status_string(error));
        return OMPI_ERROR;
    }

    module->barrier_init  = NULL;
    module->ibarrier_init = NULL;
    return OMPI_SUCCESS;
}

/*
 * Initialize module on the communicator
 */
static int mca_coll_ucx_module_enable(mca_coll_base_module_t *module,
                                      struct ompi_communicator_t *comm)
{
    mca_coll_ucx_module_t *ucx_module = (mca_coll_ucx_module_t*) module;
    int rc;

    /* Initialize some structures, e.g. datatype context, if haven't already */
    mca_common_ucx_enable();

    /* prepare the placeholder for the array of request* */
    module->base_data = OBJ_NEW(mca_coll_base_comm_t);
    if (NULL == module->base_data) {
        return OMPI_ERROR;
    }

    rc = mca_coll_ucg_create(ucx_module, comm);
    if (rc != OMPI_SUCCESS)
        return rc;

    COLL_UCX_VERBOSE(1, "UCX Collectives Module initialized");

    return OMPI_SUCCESS;
}

#define MCA_COLL_UCX_MODULE_SET_COLL_INTERNAL_SINGLE(_module, _upper_name, \
                                                     _lower_name, _params, \
                                                     _want_stable_reduce, \
                                                     _stable_suffix, \
                                                     _prefix, _suffix) \
    (_params).type.modifiers = UCG_PRIMITIVE_ ## _upper_name; \
    if (strcmp(#_lower_name, "alltoallv") && \
        strcmp(#_lower_name, "alltoallw") && \
        strcmp(#_lower_name, "neighbor_alltoallv") && \
        strcmp(#_lower_name, "neighbor_alltoallw")) { \
        (_params).type.modifiers |= UCG_GROUP_COLLECTIVE_MODIFIER_TYPE_VALID; \
    } \
    \
    if (ucg_collective_is_supported(&(_params)) == UCS_OK) { \
        module->super.coll_ ## _prefix ## _lower_name ## _suffix = \
            (_want_stable_reduce) ? \
            mca_coll_ucx_ ## _prefix ## _lower_name ## _stable_suffix ## _suffix : \
            mca_coll_ucx_ ## _prefix ## _lower_name ## _suffix; \
    }

#define MCA_COLL_UCX_MODULE_SET_COLL_INTERNAL(_module, _upper_name, _lower_name, \
                                              _params, _want_stable_reduce, \
                                              _stable_suffix) \
MCA_COLL_UCX_MODULE_SET_COLL_INTERNAL_SINGLE(_module, _upper_name, _lower_name, \
                                             _params, _want_stable_reduce, \
                                             _stable_suffix, , ) \
MCA_COLL_UCX_MODULE_SET_COLL_INTERNAL_SINGLE(_module, _upper_name, _lower_name, \
                                             _params, _want_stable_reduce, \
                                             _stable_suffix, i, ) \
MCA_COLL_UCX_MODULE_SET_COLL_INTERNAL_SINGLE(_module, _upper_name, _lower_name, \
                                             _params, _want_stable_reduce, \
                                             _stable_suffix, , _init)

#define MCA_COLL_UCX_MODULE_SET_COLL(_module, _upper_name, _lower_name, _params) \
    MCA_COLL_UCX_MODULE_SET_COLL_INTERNAL(_module, _upper_name, _lower_name, \
                                          _params, 0, )

#define MCA_COLL_UCX_MODULE_SET_COLL_STABLE(_module, _upper_name, _lower_name, \
                                            _params, _want_stable_reduce) \
    MCA_COLL_UCX_MODULE_SET_COLL_INTERNAL(_module, _upper_name, _lower_name, \
                                          _params, _want_stable_reduce, _stable)

static void mca_coll_ucx_module_construct(mca_coll_ucx_module_t *module)
{
    ucg_collective_support_params_t params = {
        .query = UCG_COLLECTIVE_SUPPORT_QUERY_BY_TYPE
    };

    memset(&module->super.super + 1, 0,
           sizeof(*module) - sizeof(module->super.super));

    module->super.coll_module_enable = mca_coll_ucx_module_enable;

    MCA_COLL_UCX_MODULE_SET_COLL(module, ALLGATHER,           allgather,           params);
    MCA_COLL_UCX_MODULE_SET_COLL(module, ALLGATHERV,          allgatherv,          params);
    MCA_COLL_UCX_MODULE_SET_COLL(module, ALLTOALL,            alltoall,            params);
    MCA_COLL_UCX_MODULE_SET_COLL(module, ALLTOALLV,           alltoallv,           params);
    MCA_COLL_UCX_MODULE_SET_COLL(module, ALLTOALLW,           alltoallw,           params);
    MCA_COLL_UCX_MODULE_SET_COLL(module, BARRIER,             barrier,             params);
    MCA_COLL_UCX_MODULE_SET_COLL(module, BCAST,               bcast,               params);
    MCA_COLL_UCX_MODULE_SET_COLL(module, GATHER,              gather,              params);
    MCA_COLL_UCX_MODULE_SET_COLL(module, GATHERV,             gatherv,             params);
    MCA_COLL_UCX_MODULE_SET_COLL(module, SCATTER,             scatter,             params);
    MCA_COLL_UCX_MODULE_SET_COLL(module, SCATTERV,            scatterv,            params);
    MCA_COLL_UCX_MODULE_SET_COLL(module, NEIGHBOR_ALLGATHER,  neighbor_allgather,  params);
    MCA_COLL_UCX_MODULE_SET_COLL(module, NEIGHBOR_ALLGATHERV, neighbor_allgatherv, params);
    MCA_COLL_UCX_MODULE_SET_COLL(module, NEIGHBOR_ALLTOALL,   neighbor_alltoall,   params);
    MCA_COLL_UCX_MODULE_SET_COLL(module, NEIGHBOR_ALLTOALLV,  neighbor_alltoallv,  params);
    MCA_COLL_UCX_MODULE_SET_COLL(module, NEIGHBOR_ALLTOALLW,  neighbor_alltoallw,  params);

    MCA_COLL_UCX_MODULE_SET_COLL_STABLE(module, ALLREDUCE, allreduce, params,
                                        mca_coll_ucx_component.stable_reduce);
    MCA_COLL_UCX_MODULE_SET_COLL_STABLE(module, EXSCAN, exscan, params,
                                        mca_coll_ucx_component.stable_reduce);
    MCA_COLL_UCX_MODULE_SET_COLL_STABLE(module, REDUCE, reduce, params,
                                        mca_coll_ucx_component.stable_reduce);
    MCA_COLL_UCX_MODULE_SET_COLL_STABLE(module, REDUCE_SCATTER, reduce_scatter, params,
                                        mca_coll_ucx_component.stable_reduce);
    MCA_COLL_UCX_MODULE_SET_COLL_STABLE(module, REDUCE_SCATTER_BLOCK, reduce_scatter_block, params,
                                        mca_coll_ucx_component.stable_reduce);
    MCA_COLL_UCX_MODULE_SET_COLL_STABLE(module, SCAN, scan, params,
                                        mca_coll_ucx_component.stable_reduce);

    /*
        Not supported yet:
        ==================
        mca_coll_base_module_reduce_local_fn_t coll_reduce_local;
        mca_coll_base_module_2_4_0_t *coll_reduce_local_module;
        mca_coll_base_module_agree_fn_t coll_agree;
        mca_coll_base_module_2_4_0_t *coll_agree_module;
        mca_coll_base_module_iagree_fn_t coll_iagree;
        mca_coll_base_module_2_4_0_t *coll_iagree_module;
    */
}

static void mca_coll_ucx_module_destruct(mca_coll_ucx_module_t *module)
{
    if (module->barrier_init != NULL) {
        ucg_collective_destroy(module->barrier_init);
    }

    if (module->ibarrier_init != NULL) {
        ucg_collective_destroy(module->ibarrier_init);
    }

    ucg_group_destroy(module->ucg_group);
}

OBJ_CLASS_INSTANCE(mca_coll_ucx_module_t,
                   mca_coll_base_module_t,
                   mca_coll_ucx_module_construct,
                   mca_coll_ucx_module_destruct);
