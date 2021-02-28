/*
 * Copyright (c) 2011 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015 Los Alamos National Security, LLC. All rights reserved.
 * Copyright (c) 2020 Huawei Technologies Co., Ltd. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "coll_ucc.h"
#include "ompi/op/op.h"

#define UCC_CONFIG_PREFIX "MPI"

static int mca_coll_ucc_component_open(void);
static int mca_coll_ucc_component_close(void);
static int mca_coll_ucc_component_register(void);
int mca_coll_ucc_init_query(bool enable_progress_threads,
                            bool enable_mpi_threads);
mca_coll_base_module_t *
mca_coll_ucc_comm_query(struct ompi_communicator_t *comm, int *priority);

mca_coll_ucc_component_t mca_coll_ucc_component = {

    /* First, the mca_component_t struct containing meta information
       about the component itfca */
    {
        .collm_version = {
            MCA_COLL_BASE_VERSION_2_0_0,

            /* Component name and version */
            .mca_component_name = "ucc",
            MCA_BASE_MAKE_VERSION(component, OMPI_MAJOR_VERSION,
                                  OMPI_MINOR_VERSION, OMPI_RELEASE_VERSION),

            /* Component open and close functions */
            .mca_open_component            = mca_coll_ucc_component_open,
            .mca_close_component           = mca_coll_ucc_component_close,
            .mca_query_component           = NULL,
            .mca_register_component_params = mca_coll_ucc_component_register,
        },
        .collm_data = {
            /* The component is not checkpoint ready */
            MCA_BASE_METADATA_PARAM_NONE
        },

        /* Initialization / querying functions */

        .collm_init_query = mca_coll_ucc_init_query,
        .collm_comm_query = mca_coll_ucc_comm_query,
    },
    90,   /* priority */
    NULL, /* UCC library handle */
    NULL  /* UCC context handle */
};

/*
 * Initial query function that is invoked during MPI_INIT, allowing
 * this component to disqualify itself if it doesn't support the
 * required level of thread support.
 */
int mca_coll_ucc_init_query(bool enable_progress_threads,
                            bool enable_mpi_threads)
{
    // TODO: store 'enable_mpi_threads' to pass during UCC initialization
    return OMPI_SUCCESS;
}

/*
 * Invoked when there's a new communicator that has been created.
 * Look at the communicator and decide which set of functions and
 * priority we want to return.
 */
mca_coll_base_module_t *
mca_coll_ucc_comm_query(struct ompi_communicator_t *comm, int *priority)
{
    /* create a new module for this communicator */
    COLL_UCC_VERBOSE(10,"Creating ucc_context for comm %p, comm_id %d, comm_size %d",
                     (void*)comm, comm->c_contextid, ompi_comm_size(comm));
    mca_coll_ucc_module_t *ucc_module = OBJ_NEW(mca_coll_ucc_module_t);
    if (!ucc_module) {
        return NULL;
    }

    *priority = mca_coll_ucc_component.priority;
    return &(ucc_module->super);
}

static int mca_coll_ucc_component_register(void)
{
    mca_coll_ucc_component.priority = 90;
    (void) mca_base_component_var_register(&mca_coll_ucc_component.super.collm_version,
                                           "priority",
                                           "Priority of the UCC component",
                                           MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                           OPAL_INFO_LVL_3,
                                           MCA_BASE_VAR_SCOPE_LOCAL,
                                           &mca_coll_ucc_component.priority);

    return OMPI_SUCCESS;
}

static void mca_coll_ucc_reduction_wrapper(void *invec, void *inoutvec,
                                          ucc_count_t *count, void *dtype,
                                          void *custom_reduction_op)
{
    ompi_op_reduce(custom_reduction_op, invec, inoutvec, *(int*)count, dtype);
}

static int mca_coll_ucc_close(void)
{
    int rc;
    ucc_status_t status;

    rc = OMPI_SUCCESS;

    status = ucc_context_destroy(mca_coll_ucc_component.ucc_context);
    if (status != UCC_OK) {
        COLL_UCC_ERROR("ucc_context_destroy failed");
        rc = OMPI_ERROR;
    }

    status = ucc_finalize(mca_coll_ucc_component.ucc_lib);
    if (status != UCC_OK) {
        COLL_UCC_ERROR("ucc_finalize failed");
        rc = OMPI_ERROR;
    }

    return rc;
}

static int mca_coll_ucc_init(void)
{
    int rc;
    ucc_status_t status;
    ucc_lib_params_t lib_params;
    ucc_lib_config_h lib_config;
    ucc_context_params_t ctx_params;
    ucc_context_config_h ctx_config;
    ucc_context_attr_t ctx_attr;

/*
    ucc_context_existing_cl_obj_t exiting[] = {
            { "ucp", opal_common_ucx.ucp_worker  },
            { "ucg", opal_common_ucx.ucg_context },
            { NULL }
    };
*/

    status = ucc_lib_config_read(UCC_CONFIG_PREFIX, NULL, &lib_config);
    if (status != UCC_OK) {
        COLL_UCC_ERROR("ucc_lib_config_read failed");
        return -1;
    }

    lib_params.mask              = UCC_LIB_PARAM_FIELD_THREAD_MODE |
                                   UCC_LIB_PARAM_FIELD_SYNC_TYPE |
                                   UCC_LIB_PARAM_FIELD_REDUCTION_WRAPPER;
    lib_params.thread_mode       = UCC_THREAD_SINGLE;
    lib_params.sync_type         = UCC_NO_SYNC_COLLECTIVES;
    lib_params.reduction_wrapper = mca_coll_ucc_reduction_wrapper;

    status = ucc_init(&lib_params, lib_config, &mca_coll_ucc_component.ucc_lib);

    ucc_lib_config_release(lib_config);

    if (status != UCC_OK) {
        COLL_UCC_ERROR("ucc_init failed");
        return -1;
    }

    status = ucc_context_config_read(mca_coll_ucc_component.ucc_lib, NULL, &ctx_config);
    if (status != UCC_OK) {
        COLL_UCC_ERROR("ucc_context_config_read failed");
        ucc_finalize(mca_coll_ucc_component.ucc_lib);
        return -1;
    }

    ctx_params.mask      = UCC_CONTEXT_PARAM_FIELD_TYPE |
                           UCC_CONTEXT_PARAM_FIELD_COLL_SYNC_TYPE /*|
                           UCC_CONTEXT_PARAM_FIELD_EXISTING_CL_OBJECTS*/;
    ctx_params.ctx_type  = UCC_CONTEXT_EXCLUSIVE;
    ctx_params.sync_type = UCC_NO_SYNC_COLLECTIVES;
    /* ctx_params.ctx_obj   = exiting;*/

    status = ucc_context_create(mca_coll_ucc_component.ucc_lib,
                                &ctx_params, ctx_config,
                                &mca_coll_ucc_component.ucc_context);

    ucc_context_config_release(ctx_config);

    if (status != UCC_OK) {
        COLL_UCC_ERROR("ucc_context_create failed");
        ucc_finalize(mca_coll_ucc_component.ucc_lib);
        return -1;
    }

    status = ucc_context_get_attr(mca_coll_ucc_component.ucc_context, &ctx_attr);
    if ((status != UCC_OK) ||
        ((ctx_attr.mask & UCC_CONTEXT_ATTR_FIELD_CONTEXT_ADDR) == 0) ||
        ((ctx_attr.mask & UCC_CONTEXT_ATTR_FIELD_CONTEXT_ADDR_LEN) == 0)) {
        COLL_UCC_ERROR("ucc_context_get_attr failed");
        mca_coll_ucc_close();
        return -1;
    }

    OPAL_MODEX_SEND(rc, PMIX_GLOBAL, &mca_coll_ucc_component.super.collm_version,
                    (void*)ctx_attr.ctx_addr, (size_t)ctx_attr.ctx_addr_len);

    if (rc != OPAL_SUCCESS) {
        mca_coll_ucc_close();
    }

    return rc;
}

static int mca_coll_ucc_component_open(void)
{
    int rc;
    size_t dummy;

    COLL_UCC_VERBOSE(1, "mca_coll_ucc_component_open");

    opal_common_ucx_mca_register();

    rc = mca_common_ucx_open("MPI", &dummy);
    if (rc < 0) {
        return rc;
    }

    rc = mca_common_ucx_init(&mca_coll_ucc_component.super.collm_version);
    if (rc < 0) {
        mca_common_ucx_close();
    }

    rc = mca_coll_ucc_init();
    if (rc < 0) {
        mca_common_ucx_close();
    }

    return rc;
}

static int mca_coll_ucc_component_close(void)
{
    int rc;

    COLL_UCC_VERBOSE(1, "mca_coll_ucc_component_close");

    rc = mca_coll_ucc_close();

    (void) mca_common_ucx_close();

    opal_common_ucx_mca_deregister();

    return rc;
}
