/*
 * Copyright (c) 2011 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2014 Research Organization for Information Science and
 *                    Technology (RIST). All rights reserved.
 * Copyright (c) 2020 Huawei Technologies Co., Ltd. All rights reserved.
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

#include "coll_ucc.h"

static ucc_status_t mca_coll_ucc_conn_info_lookup(void *conn_ctx, uint64_t ep,
                                                  ucc_p2p_conn_t **conn_info,
                                                  void *request)
{
    int ret;
    size_t dummy;

    /* Sanity checks */
    ompi_communicator_t* comm = (ompi_communicator_t*)conn_ctx;
    if (ep == (uint64_t)comm->c_my_rank) {
        COLL_UCC_ERROR("mca_common_ucx_resolve_address(rank=%lu)"
                       "shouldn't be called on its own rank (loopback)", ep);
        return 1;
    }

    /* Check the cache for a previously established connection to that rank */
    ompi_proc_t *proc_peer = ompi_comm_peer_lookup(comm, ep);
    *conn_info = proc_peer->proc_endpoints[OMPI_PROC_ENDPOINT_TAG_UCC];
    if (*conn_info) {
       return 0;
    }

    /* Obtain the UCC address of the remote */
    OPAL_MODEX_RECV(ret, &mca_coll_ucc_component.super.collm_version,
                    &proc_peer->super.proc_name, (void**)conn_info, &dummy);
    if (ret < 0) {
        COLL_UCC_ERROR("Failed to receive remote UCC address: %s (%d)",
                       opal_strerror(ret), ret);
        return 1;
    }

    COLL_UCC_VERBOSE(2, "Got proc %d address, size %ld",
                     proc_peer->super.proc_name.vpid, dummy);

    /* Cache the connection for future invocations with this rank */
    proc_peer->proc_endpoints[OMPI_PROC_ENDPOINT_TAG_UCC] = *conn_info;
    return 0;
}

static ucc_status_t mca_coll_ucc_conn_info_release(ucc_p2p_conn_t *conn_info)
{
    return UCC_OK; /* the address is stored in proc_peer->proc_endpoints */
}

static ucc_status_t mca_coll_ucc_conn_req_test(void *request)
{
    return UCC_OK; /* request is never used since info lookup is blocking */
}

static ucc_status_t mca_coll_ucc_conn_req_free(void *request)
{
    return UCC_OK; /* request is never used since info lookup is blocking */
}

static int mca_coll_ucc_team_create(mca_coll_ucc_module_t *module,
                                    struct ompi_communicator_t *comm)
{
    ucc_status_t error;
    ucc_team_params_t params;

    params.mask                       = UCC_TEAM_PARAM_FIELD_EP |
                                        UCC_TEAM_PARAM_FIELD_TEAM_SIZE |
                                        UCC_TEAM_PARAM_FIELD_SYNC_TYPE |
                                        UCC_TEAM_PARAM_FIELD_P2P_CONN;
    params.ep                         = ompi_comm_rank(comm);
    params.team_size                  = ompi_comm_size(comm);
    params.sync_type                  = UCC_NO_SYNC_COLLECTIVES;
    params.p2p_conn.conn_info_lookup  = mca_coll_ucc_conn_info_lookup;
    params.p2p_conn.conn_info_release = mca_coll_ucc_conn_info_release;
    params.p2p_conn.conn_ctx          = comm;
    params.p2p_conn.req_test          = mca_coll_ucc_conn_req_test;
    params.p2p_conn.req_free          = mca_coll_ucc_conn_req_free;

    error = ucc_team_create_post(&mca_coll_ucc_component.ucc_context, 1,
                                 &params, &module->ucc_team);

    /* Examine comm_new return value */
    if (error != UCC_OK) {
        COLL_UCC_ERROR("ucc_team_create_post failed: %s", ucc_status_string(error));
        return OMPI_ERROR;
    }

    MCA_COMMON_UCX_WAIT_LOOP(module->ucc_team,
                             OPAL_COMMON_UCX_REQUEST_TYPE_UCC_TEAM,
                             opal_common_ucx.ucp_worker, "ucc team", NULL);
}

/*
 * Initialize module on the communicator
 */
static int mca_coll_ucc_module_enable(mca_coll_base_module_t *module,
                                      struct ompi_communicator_t *comm)
{
    mca_coll_ucc_module_t *ucc_module = (mca_coll_ucc_module_t*) module;
    int rc;

    /* Initialize some structures, e.g. datatype context, if haven't already */
    mca_common_ucx_enable();

    /* prepare the placeholder for the array of request* */
    module->base_data = OBJ_NEW(mca_coll_base_comm_t);
    if (NULL == module->base_data) {
        return OMPI_ERROR;
    }

    rc = mca_coll_ucc_team_create(ucc_module, comm);
    if (rc != OMPI_SUCCESS)
        return rc;

    COLL_UCC_VERBOSE(1, "UCC Collectives Module initialized");

    return OMPI_SUCCESS;
}

static void mca_coll_ucc_module_construct(mca_coll_ucc_module_t *module)
{
    memset(&module->super.super + 1, 0,
           sizeof(*module) - sizeof(module->super.super));

    module->super.coll_module_enable  = mca_coll_ucc_module_enable;
    module->super.coll_barrier        = mca_coll_ucc_barrier;
    module->super.coll_bcast          = mca_coll_ucc_bcast;
    module->super.coll_reduce         = mca_coll_ucc_reduce;
    module->super.coll_allreduce      = mca_coll_ucc_allreduce;
}

static void mca_coll_ucc_module_destruct(mca_coll_ucc_module_t *module) {
    ucc_team_destroy(module->ucc_team);
}

OBJ_CLASS_INSTANCE(mca_coll_ucc_module_t,
                   mca_coll_base_module_t,
                   mca_coll_ucc_module_construct,
                   mca_coll_ucc_module_destruct);
