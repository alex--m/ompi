/*
 * Copyright (c) 2011 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015 Los Alamos National Security, LLC. All rights reserved.
 * Copyright (c) 2019 Huawei Technologies Co., Ltd. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "coll_ucx.h"
#include "coll_ucx_request.h"

static int ucx_open(void);
static int ucx_close(void);
static int ucx_register(void);
int mca_coll_ucx_init_query(bool enable_progress_threads,
                            bool enable_mpi_threads);
mca_coll_base_module_t *
mca_coll_ucx_comm_query(struct ompi_communicator_t *comm, int *priority);

mca_coll_ucx_component_t mca_coll_ucx_component = {

    /* First, the mca_component_t struct containing meta information
       about the component itfca */
    {
        .collm_version = {
            MCA_COLL_BASE_VERSION_2_0_0,

            /* Component name and version */
            .mca_component_name = "ucx",
            MCA_BASE_MAKE_VERSION(component, OMPI_MAJOR_VERSION, OMPI_MINOR_VERSION,
                                  OMPI_RELEASE_VERSION),

            /* Component open and close functions */
            .mca_open_component = ucx_open,
            .mca_close_component = ucx_close,
            .mca_register_component_params = ucx_register,
        },
        .collm_data = {
            /* The component is not checkpoint ready */
            MCA_BASE_METADATA_PARAM_NONE
        },

        /* Initialization / querying functions */

        .collm_init_query = mca_coll_ucx_init_query,
        .collm_comm_query = mca_coll_ucx_comm_query,
    },
    91,  /* priority */
    0,   /* verbose level */
    0,   /* ucx_enable */
    NULL /* UCX version */
};

/*
 * Initial query function that is invoked during MPI_INIT, allowing
 * this component to disqualify itself if it doesn't support the
 * required level of thread support.
 */
int mca_coll_ucx_init_query(bool enable_progress_threads,
                            bool enable_mpi_threads)
{
    return OMPI_SUCCESS;
}

/*
 * Invoked when there's a new communicator that has been created.
 * Look at the communicator and decide which set of functions and
 * priority we want to return.
 */
mca_coll_base_module_t *
mca_coll_ucx_comm_query(struct ompi_communicator_t *comm, int *priority)
{
   /* basic checks */
    if ((OMPI_COMM_IS_INTER(comm)) || (ompi_comm_size(comm) < 2)) {
        return NULL;
    }

    /* create a new module for this communicator */
    COLL_UCX_VERBOSE(10,"Creating ucx_context for comm %p, comm_id %d, comm_size %d",
                 (void*)comm, comm->c_contextid, ompi_comm_size(comm));
    mca_coll_ucx_module_t *ucx_module = OBJ_NEW(mca_coll_ucx_module_t);
    if (!ucx_module) {
        return NULL;
    }

    *priority = mca_coll_ucx_component.priority;
    return &(ucx_module->super);
}

static int ucx_register(void)
{
    mca_coll_ucx_component.verbose = 0;
    (void) mca_base_component_var_register(&mca_coll_ucx_component.super.collm_version, "verbosity",
                                           "Verbosity of the UCX component",
                                           MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                           OPAL_INFO_LVL_3,
                                           MCA_BASE_VAR_SCOPE_LOCAL,
                                           &mca_coll_ucx_component.verbose);

    mca_coll_ucx_component.priority = 91;
    (void) mca_base_component_var_register(&mca_coll_ucx_component.super.collm_version, "priority",
                                           "Priority of the UCX component",
                                           MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                           OPAL_INFO_LVL_3,
                                           MCA_BASE_VAR_SCOPE_LOCAL,
                                           &mca_coll_ucx_component.priority);

    mca_coll_ucx_component.num_disconnect = 1;
    (void) mca_base_component_var_register(&mca_coll_ucx_component.super.collm_version, "num_disconnect",
                                           "How may disconnects go in parallel",
                                           MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                           OPAL_INFO_LVL_3,
                                           MCA_BASE_VAR_SCOPE_LOCAL,
                                           &mca_coll_ucx_component.num_disconnect);

    opal_common_ucx_mca_var_register(&mca_coll_ucx_component.super.collm_version);
    return OMPI_SUCCESS;
}

static int ucx_open(void)
{
    mca_coll_ucx_component.output = opal_output_open(NULL);
    opal_output_set_verbosity(mca_coll_ucx_component.output,
          mca_coll_ucx_component.verbose);

    opal_common_ucx_mca_register();

    return mca_coll_ucx_open();
}

static int ucx_close(void)
{
   (void) mca_coll_ucx_cleanup();

    opal_common_ucx_mca_deregister();

    return mca_coll_ucx_close();
}

#if HAVE_UCP_WORKER_ADDRESS_FLAGS
static int mca_coll_ucx_send_worker_address_type(int addr_flags, int modex_scope)
{
    ucs_status_t status;
    ucp_worker_attr_t attrs;
    int rc;

    attrs.field_mask    = UCP_WORKER_ATTR_FIELD_ADDRESS |
                          UCP_WORKER_ATTR_FIELD_ADDRESS_FLAGS;
    attrs.address_flags = addr_flags;

    status = ucp_worker_query(mca_coll_ucx_component.ucp_worker, &attrs);
    if (UCS_OK != status) {
        COLL_UCX_ERROR("Failed to query UCP worker address");
        return OMPI_ERROR;
    }

    OPAL_MODEX_SEND(rc, modex_scope, &mca_coll_ucx_component.super.collm_version,
                    (void*)attrs.address, attrs.address_length);

    ucp_worker_release_address(mca_coll_ucx_component.ucp_worker, attrs.address);

    if (OMPI_SUCCESS != rc) {
        return OMPI_ERROR;
    }

    COLL_UCX_VERBOSE(2, "Pack %s worker address, size %ld",
                     (modex_scope == PMIX_LOCAL) ? "local" : "remote",
                     attrs.address_length);

    return OMPI_SUCCESS;
}
#endif

static int mca_coll_ucx_send_worker_address(void)
{
    ucs_status_t status;

#if !HAVE_UCP_WORKER_ADDRESS_FLAGS
    ucp_address_t *address;
    size_t addrlen;
    int rc;

    status = ucp_worker_get_address(ompi_coll_ucx.ucp_worker, &address, &addrlen);
    if (UCS_OK != status) {
        COLL_UCX_ERROR("Failed to get worker address");
        return OMPI_ERROR;
    }

    COLL_UCX_VERBOSE(2, "Pack worker address, size %ld", addrlen);

    OPAL_MODEX_SEND(rc, PMIX_GLOBAL,
                    &mca_coll_ucx_component.super.collm_version, (void*)address, addrlen);

    ucp_worker_release_address(ompi_coll_ucx.ucp_worker, address);

    if (OMPI_SUCCESS != rc) {
        goto err;
    }
#else
    /* Pack just network device addresses for remote node peers */
    status = mca_coll_ucx_send_worker_address_type(UCP_WORKER_ADDRESS_FLAG_NET_ONLY,
                                                   PMIX_REMOTE);
    if (UCS_OK != status) {
        goto err;
    }

    status = mca_coll_ucx_send_worker_address_type(0, PMIX_LOCAL);
    if (UCS_OK != status) {
        goto err;
    }
#endif

    return OMPI_SUCCESS;

err:
    COLL_UCX_ERROR("Open MPI couldn't distribute EP connection details");
    return OMPI_ERROR;
}

static int mca_coll_ucx_recv_worker_address(ompi_proc_t *proc,
                                            ucp_address_t **address_p,
                                            size_t *addrlen_p)
{
    int ret;

    *address_p = NULL;
    OPAL_MODEX_RECV(ret, &mca_coll_ucx_component.super.collm_version, &proc->super.proc_name,
                    (void**)address_p, addrlen_p);
    if (ret < 0) {
        COLL_UCX_ERROR("Failed to receive UCX worker address: %s (%d)",
                       opal_strerror(ret), ret);
    }

    COLL_UCX_VERBOSE(2, "Got proc %d address, size %ld",
                     proc->super.proc_name.vpid, *addrlen_p);
    return ret;
}

static int mca_coll_ucg_is_dtype_int(ompi_datatype_t *dtype)
{
    /* TODO: what about signed/unsigned? */
    return (dtype->super.flags & OMPI_DATATYPE_FLAG_DATA_INT);
}

static int mca_coll_ucg_is_dtype_fp(ompi_datatype_t *dtype)
{
    return (dtype->super.flags & OMPI_DATATYPE_FLAG_DATA_FLOAT);
}

static int mca_coll_ucg_is_op_sum(ompi_op_t *op)
{
    return (op == &ompi_mpi_op_sum.op);
}

static int mca_coll_ucg_is_op_loc(ompi_op_t *op)
{
    return ((op == &ompi_mpi_op_minloc.op) ||
            (op == &ompi_mpi_op_maxloc.op));
}

int mca_coll_ucx_open(void)
{
    ucp_context_attr_t attr;
    ucp_context_h ucp_context;
    ucp_params_t ucp_params;
    ucg_params_t ucg_params;
    ucg_config_t *config;
    ucs_status_t status;

    COLL_UCX_VERBOSE(1, "mca_coll_ucx_open");

    /* Read options */
    status = ucg_config_read("MPI", NULL, &config);
    if (UCS_OK != status) {
        return OMPI_ERROR;
    }

    /* Initialize UCP context parameters */
    ucp_params.field_mask           = UCP_PARAM_FIELD_FEATURES          |
                                      UCP_PARAM_FIELD_ESTIMATED_NUM_EPS |
                                      UCP_PARAM_FIELD_GROUP_PEER_INFO;
    ucp_params.features             = UCP_FEATURE_RMA   |
                                      UCP_FEATURE_AMO32 |
                                      UCP_FEATURE_AMO64 |
                                      UCP_FEATURE_GROUPS;
    ucp_params.estimated_num_eps    = ompi_process_info.myprocid.rank;
    ucp_params.peer_info.num_local  = ompi_process_info.num_local_peers + 1;
    ucp_params.peer_info.local_idx  = ompi_process_info.my_local_rank;
    ucp_params.peer_info.num_global = ompi_process_info.num_procs;
    ucp_params.peer_info.global_idx = ompi_process_info.myprocid.rank;

    /* Initialize UCG context parameters */
    ucg_params.super                    = &ucp_params;
    ucg_params.field_mask               = UCG_PARAM_FIELD_JOB_UID      |
                                          UCG_PARAM_FIELD_ADDRESS_CB   |
                                          UCG_PARAM_FIELD_NEIGHBORS_CB |
                                          UCG_PARAM_FIELD_REDUCE_CB    |
                                          UCG_PARAM_FIELD_TYPE_INFO_CB |
                                          UCG_PARAM_FIELD_MPI_IN_PLACE |
                                          UCG_PARAM_FIELD_HANDLE_FAULT;
    ucg_params.job_uid                  = ompi_process_info.my_name.jobid;
    ucg_params.address.lookup_f         = (typeof(ucg_params.address.lookup_f))
                                          mca_coll_ucx_resolve_address;
    ucg_params.address.release_f        = (typeof(ucg_params.address.release_f))
                                          mca_coll_ucx_release_address;
    ucg_params.neighbors.vertex_count_f = (typeof(ucg_params.neighbors.vertex_count_f))
                                          mca_coll_ucx_neighbors_count;
    ucg_params.neighbors.vertex_query_f = (typeof(ucg_params.neighbors.vertex_query_f))
                                          mca_coll_ucx_neighbors_query;
    ucg_params.mpi_reduce_f             = (typeof(ucg_params.mpi_reduce_f))
                                          ompi_op_reduce;
    ucg_params.mpi_in_place             = (void*)MPI_IN_PLACE;
    ucg_params.type_info.mpi_is_int_f   = (typeof(ucg_params.type_info.mpi_is_int_f))
                                          mca_coll_ucg_is_dtype_int;
    ucg_params.type_info.mpi_is_fp_f    = (typeof(ucg_params.type_info.mpi_is_fp_f))
                                          mca_coll_ucg_is_dtype_fp;
    ucg_params.type_info.mpi_is_sum_f   = (typeof(ucg_params.type_info.mpi_is_sum_f))
                                          mca_coll_ucg_is_op_sum;
    ucg_params.type_info.mpi_is_loc_f   = (typeof(ucg_params.type_info.mpi_is_loc_f))
                                          mca_coll_ucg_is_op_loc;
    ucg_params.fault.mode               = UCG_FAULT_IS_FATAL;

    status = ucg_init(&ucg_params, config, &mca_coll_ucx_component.ucg_context);
    ucg_config_release(config);

    if (UCS_OK != status) {
        return OMPI_ERROR;
    }

    /* Query UCX attributes */
    attr.field_mask = UCP_ATTR_FIELD_REQUEST_SIZE;
    ucp_context = ucg_context_get_ucp(mca_coll_ucx_component.ucg_context);
    status = ucp_context_query(ucp_context, &attr);
    if (UCS_OK != status) {
       goto out;
    }

    mca_coll_ucx_component.request_size = attr.request_size;

    /* Initialize UCX worker */
    if (OMPI_SUCCESS != mca_coll_ucx_init()) {
       goto out;
    }

    ucs_list_head_init(&mca_coll_ucx_component.group_head);
    return OMPI_SUCCESS;

out:
    ucg_cleanup(mca_coll_ucx_component.ucg_context);
    mca_coll_ucx_component.ucg_context = NULL;
    return OMPI_ERROR;
}

int mca_coll_ucx_close(void)
{
    COLL_UCX_VERBOSE(1, "mca_coll_ucx_close");

    if (mca_coll_ucx_component.ucp_worker != NULL) {
       mca_coll_ucx_cleanup();
       mca_coll_ucx_component.ucp_worker = NULL;
    }

    if (mca_coll_ucx_component.ucg_context != NULL) {
        ucg_cleanup(mca_coll_ucx_component.ucg_context);
        mca_coll_ucx_component.ucg_context = NULL;
    }
    return OMPI_SUCCESS;
}

int mca_coll_ucx_progress(void)
{
    mca_coll_ucx_module_t *module;
    ucs_list_for_each(module, &mca_coll_ucx_component.group_head, ucs_list) {
        ucg_group_progress(module->ucg_group);
    }
    return OMPI_SUCCESS;
}

int mca_coll_ucx_init(void)
{
    ucp_context_h ucp_context = ucg_context_get_ucp(mca_coll_ucx_component.ucg_context);
    ucp_worker_params_t params;
    ucp_worker_attr_t attr;
    ucs_status_t status;
    int rc;

    COLL_UCX_VERBOSE(1, "mca_coll_ucx_init");

    /* TODO check MPI thread mode */
    params.field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    if (ompi_mpi_thread_multiple) {
        params.thread_mode = UCS_THREAD_MODE_MULTI;
    } else {
        params.thread_mode = UCS_THREAD_MODE_SINGLE;
    }

    status = ucp_worker_create(ucp_context, &params, &mca_coll_ucx_component.ucp_worker);
    if (UCS_OK != status) {
        COLL_UCX_ERROR("Failed to create UCP worker");
        rc = OMPI_ERROR;
        goto err;
    }

    attr.field_mask = UCP_WORKER_ATTR_FIELD_THREAD_MODE;
    status = ucp_worker_query(mca_coll_ucx_component.ucp_worker, &attr);
    if (UCS_OK != status) {
        COLL_UCX_ERROR("Failed to query UCP worker thread level");
        rc = OMPI_ERROR;
        goto err_destroy_worker;
    }

    /* UCX does not support multithreading, disqualify current COLL for now */
    if (ompi_mpi_thread_multiple && (attr.thread_mode != UCS_THREAD_MODE_MULTI)) {
        /* TODO: we should let OMPI to fallback to THREAD_SINGLE mode */
        COLL_UCX_ERROR("UCP worker does not support MPI_THREAD_MULTIPLE");
        rc = OMPI_ERR_NOT_SUPPORTED;
        goto err_destroy_worker;
    }

    /* Share my UCP address, so it could be later obtained via @ref mca_coll_ucx_resolve_address */
    rc = mca_coll_ucx_send_worker_address();
    if (rc < 0) {
        goto err_destroy_worker;
    }

    /* Initialize the free lists */
    OBJ_CONSTRUCT(&mca_coll_ucx_component.persistent_ops, mca_coll_ucx_freelist_t);

    /* Create a completed request to be returned from isend */
    OBJ_CONSTRUCT(&mca_coll_ucx_component.completed_send_req, ompi_request_t);
    mca_coll_ucx_completed_request_init(&mca_coll_ucx_component.completed_send_req);

    opal_progress_register(mca_coll_ucx_progress);

    COLL_UCX_VERBOSE(2, "created ucp context %p, worker %p",
                    (void *)mca_coll_ucx_component.ucg_context,
                    (void *)mca_coll_ucx_component.ucp_worker);
    return rc;

err_destroy_worker:
    ucp_worker_destroy(mca_coll_ucx_component.ucp_worker);
    mca_coll_ucx_component.ucp_worker = NULL;
err:
    return OMPI_ERROR;
}

void mca_coll_ucx_cleanup(void)
{
    COLL_UCX_VERBOSE(1, "mca_coll_ucx_cleanup");

    opal_progress_unregister(mca_coll_ucx_progress);

    mca_coll_ucx_component.completed_send_req.req_state = OMPI_REQUEST_INVALID;
    OMPI_REQUEST_FINI(&mca_coll_ucx_component.completed_send_req);
    OBJ_DESTRUCT(&mca_coll_ucx_component.completed_send_req);
    OBJ_DESTRUCT(&mca_coll_ucx_component.persistent_ops);

    if (mca_coll_ucx_component.ucp_worker) {
        ucp_worker_destroy(mca_coll_ucx_component.ucp_worker);
        mca_coll_ucx_component.ucp_worker = NULL;
    }
}

int mca_coll_ucx_resolve_address(void *cb_group_obj,
                                 ucg_group_member_index_t rank,
                                 ucp_address_t **addr,
                                 size_t *addr_len)
{
    /* Sanity checks */
    ompi_communicator_t* comm = (ompi_communicator_t*)cb_group_obj;
    if (rank == (ucg_group_member_index_t)comm->c_my_rank) {
        COLL_UCX_ERROR("mca_coll_ucx_resolve_address(rank=%lu)"
                       "shouldn't be called on its own rank (loopback)", rank);
        return 1;
    }

    /* Check the cache for a previously established connection to that rank */
    ompi_proc_t *proc_peer =
          (struct ompi_proc_t*)ompi_comm_peer_lookup((ompi_communicator_t*)cb_group_obj, rank);
    *addr = proc_peer->proc_endpoints[OMPI_PROC_ENDPOINT_TAG_COLL];
    *addr_len = 0; /* UCX doesn't need the length to unpack the address */
    if (*addr) {
       return 0;
    }

    /* Obtain the UCP address of the remote */
    int ret = mca_coll_ucx_recv_worker_address(proc_peer, addr, addr_len);
    if (ret < 0) {
        COLL_UCX_ERROR("mca_coll_ucx_recv_worker_address(proc=%d rank=%lu) failed",
                       proc_peer->super.proc_name.vpid, rank);
        return 1;
    }

    /* Cache the connection for future invocations with this rank */
    proc_peer->proc_endpoints[OMPI_PROC_ENDPOINT_TAG_COLL] = *addr;
    return 0;
}

void mca_coll_ucx_release_address(ucp_address_t *addr)
{
    /* no need to free - the address is stored in proc_peer->proc_endpoints */
}

ucp_worker_h mca_coll_ucx_get_component_worker()
{
    return mca_coll_ucx_component.ucp_worker;
}
