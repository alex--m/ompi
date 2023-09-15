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
#if OMPI_GROUP_SPARSE
    COLL_UCX_ERROR("Sparse process groups are not supported");
    return UCS_ERR_UNSUPPORTED;
#endif

    /* Fill in group initialization parameters */
    ucg_group_params_t args;
    args.field_mask               = UCG_GROUP_PARAM_FIELD_UCP_WORKER   |
                                    UCG_GROUP_PARAM_FIELD_MEMBER_COUNT |
                                    UCG_GROUP_PARAM_FIELD_MEMBER_INDEX |
                                    UCG_GROUP_PARAM_FIELD_CB_CONTEXT   |
                                    UCG_GROUP_PARAM_FIELD_DISTANCES    |
                                    UCG_GROUP_PARAM_FIELD_NAME         |
                                    UCG_GROUP_PARAM_FIELD_FLAGS;
    args.worker                   = opal_common_ucx.ucp_worker;
    args.member_count             = ompi_comm_size(comm);
    args.member_index             = ompi_comm_rank(comm);
    args.cb_context               = comm;
    args.name                     = comm->c_name;
    args.flags                    = 0;
    args.distance_type            = UCG_GROUP_DISTANCE_TYPE_ARRAY;
    args.distance_array           = alloca(args.member_count *
                                           sizeof(*args.distance_array));
    if (args.distance_array == NULL) {
        COLL_UCX_ERROR("Failed to allocate memory for %u local ranks", args.member_count);
        return OMPI_ERROR;
    }

    /* Generate (temporary) rank-distance array */
    ucg_group_member_index_t rank_idx;
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

    ucs_status_t error = ucg_group_create(&args, &module->ucg_group);

    /* Examine comm_new return value */
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

static void mca_coll_ucx_module_construct(mca_coll_ucx_module_t *module)
{
    memset(&module->super.super + 1, 0,
           sizeof(*module) - sizeof(module->super.super));

    module->super.coll_module_enable             = mca_coll_ucx_module_enable;

    /* blocking functions */
    module->super.coll_allgather                 = mca_coll_ucx_allgather;
    module->super.coll_allgatherv                = mca_coll_ucx_allgatherv;
    module->super.coll_allreduce                 = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_allreduce_stable :
                                                   mca_coll_ucx_allreduce;
    module->super.coll_alltoall                  = mca_coll_ucx_alltoall;
    module->super.coll_alltoallv                 = mca_coll_ucx_alltoallv;
    module->super.coll_alltoallw                 = mca_coll_ucx_alltoallw;
    module->super.coll_barrier                   = mca_coll_ucx_barrier;
    module->super.coll_bcast                     = mca_coll_ucx_bcast;
    module->super.coll_exscan                    = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_exscan_stable :
                                                   mca_coll_ucx_exscan;
    module->super.coll_gather                    = mca_coll_ucx_gather;
    module->super.coll_gatherv                   = mca_coll_ucx_gatherv;
    module->super.coll_reduce                    = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_reduce_stable :
                                                   mca_coll_ucx_reduce;
    module->super.coll_reduce_scatter            = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_reduce_scatter_stable :
                                                   mca_coll_ucx_reduce_scatter;
    module->super.coll_reduce_scatter_block      = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_reduce_scatter_block_stable :
                                                   mca_coll_ucx_reduce_scatter_block;
    module->super.coll_scan                      = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_scan_stable :
                                                   mca_coll_ucx_scan;
    module->super.coll_scatter                   = mca_coll_ucx_scatter;
    module->super.coll_scatterv                  = mca_coll_ucx_scatterv;

    /* nonblocking functions */
    module->super.coll_iallgather                = mca_coll_ucx_iallgather;
    module->super.coll_iallgatherv               = mca_coll_ucx_iallgatherv;
    module->super.coll_iallreduce                = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_iallreduce_stable :
                                                   mca_coll_ucx_iallreduce;
    module->super.coll_ialltoall                 = mca_coll_ucx_ialltoall;
    module->super.coll_ialltoallv                = mca_coll_ucx_ialltoallv;
    module->super.coll_ialltoallw                = mca_coll_ucx_ialltoallw;
    module->super.coll_ibarrier                  = mca_coll_ucx_ibarrier;
    module->super.coll_ibcast                    = mca_coll_ucx_ibcast;
    module->super.coll_iexscan                   = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_iexscan_stable :
                                                   mca_coll_ucx_iexscan;
    module->super.coll_igather                   = mca_coll_ucx_igather;
    module->super.coll_igatherv                  = mca_coll_ucx_igatherv;
    module->super.coll_ireduce                   = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_ireduce_stable :
                                                   mca_coll_ucx_ireduce;
    module->super.coll_ireduce_scatter           = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_ireduce_scatter_stable :
                                                   mca_coll_ucx_ireduce_scatter;
    module->super.coll_ireduce_scatter_block     = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_ireduce_scatter_block_stable :
                                                   mca_coll_ucx_ireduce_scatter_block;
    module->super.coll_iscan                     = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_iscan_stable :
                                                   mca_coll_ucx_iscan;
    module->super.coll_iscatter                  = mca_coll_ucx_iscatter;
    module->super.coll_iscatterv                 = mca_coll_ucx_iscatterv;

    /* persistent functions */
    module->super.coll_allgather_init            = mca_coll_ucx_allgather_init;
    module->super.coll_allgatherv_init           = mca_coll_ucx_allgatherv_init;
    module->super.coll_allreduce_init            = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_allreduce_stable_init :
                                                   mca_coll_ucx_allreduce_init;
    module->super.coll_alltoall_init             = mca_coll_ucx_alltoall_init;
    module->super.coll_alltoallv_init            = mca_coll_ucx_alltoallv_init;
    module->super.coll_alltoallw_init            = mca_coll_ucx_alltoallw_init;
    module->super.coll_barrier_init              = mca_coll_ucx_barrier_init;
    module->super.coll_bcast_init                = mca_coll_ucx_bcast_init;
    module->super.coll_exscan_init               = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_exscan_stable_init :
                                                   mca_coll_ucx_exscan_init;
    module->super.coll_gather_init               = mca_coll_ucx_gather_init;
    module->super.coll_gatherv_init              = mca_coll_ucx_gatherv_init;
    module->super.coll_reduce_init               = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_reduce_stable_init :
                                                   mca_coll_ucx_reduce_init;
    module->super.coll_reduce_scatter_init       = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_reduce_scatter_stable_init :
                                                   mca_coll_ucx_reduce_scatter_init;
    module->super.coll_reduce_scatter_block_init = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_reduce_scatter_block_stable_init :
                                                   mca_coll_ucx_reduce_scatter_block_init;
    module->super.coll_scan_init                 = mca_coll_ucx_component.stable_reduce ?
                                                   mca_coll_ucx_scan_stable_init :
                                                   mca_coll_ucx_scan_init;
    module->super.coll_scatter_init              = mca_coll_ucx_scatter_init;
    module->super.coll_scatterv_init             = mca_coll_ucx_scatterv_init;

    /* neighborhood functions */
    module->super.coll_neighbor_allgather        = mca_coll_ucx_neighbor_allgather;
    module->super.coll_neighbor_allgatherv       = mca_coll_ucx_neighbor_allgatherv;
    module->super.coll_neighbor_alltoall         = mca_coll_ucx_neighbor_alltoall;
    module->super.coll_neighbor_alltoallv        = mca_coll_ucx_neighbor_alltoallv;
    module->super.coll_neighbor_alltoallw        = mca_coll_ucx_neighbor_alltoallw;

    /* nonblocking neighborhood functions */
    module->super.coll_ineighbor_allgather       = mca_coll_ucx_ineighbor_allgather;
    module->super.coll_ineighbor_allgatherv      = mca_coll_ucx_ineighbor_allgatherv;
    module->super.coll_ineighbor_alltoall        = mca_coll_ucx_ineighbor_alltoall;
    module->super.coll_ineighbor_alltoallv       = mca_coll_ucx_ineighbor_alltoallv;
    module->super.coll_ineighbor_alltoallw       = mca_coll_ucx_ineighbor_alltoallw;

    /* persistent neighborhood functions */
    module->super.coll_neighbor_allgather_init   = mca_coll_ucx_neighbor_allgather_init;
    module->super.coll_neighbor_allgatherv_init  = mca_coll_ucx_neighbor_allgatherv_init;
    module->super.coll_neighbor_alltoall_init    = mca_coll_ucx_neighbor_alltoall_init;
    module->super.coll_neighbor_alltoallv_init   = mca_coll_ucx_neighbor_alltoallv_init;
    module->super.coll_neighbor_alltoallw_init   = mca_coll_ucx_neighbor_alltoallw_init;

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
