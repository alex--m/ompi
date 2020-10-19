/*
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */



static int mca_common_ucx_is_dtype_int(ompi_datatype_t *dtype)
{
    /* TODO: what about signed/unsigned? */
    return (dtype->super.flags & OMPI_DATATYPE_FLAG_DATA_INT);
}

static int mca_common_ucx_is_dtype_fp(ompi_datatype_t *dtype)
{
    return (dtype->super.flags & OMPI_DATATYPE_FLAG_DATA_FLOAT);
}

static int mca_common_ucx_is_op_sum(ompi_op_t *op)
{
    return (op == &ompi_mpi_op_sum.op);
}

static int mca_common_ucx_is_op_loc(ompi_op_t *op)
{
    return ((op == &ompi_mpi_op_minloc.op) ||
            (op == &ompi_mpi_op_maxloc.op));
}

static int mca_common_ucx_resolve_address(void *cb_group_obj,
                                        ucg_group_member_index_t rank,
                                        ucp_address_t **addr,
                                        size_t *addr_len)
{
    /* Sanity checks */
    ompi_communicator_t* comm = (ompi_communicator_t*)cb_group_obj;
    if (rank == (ucg_group_member_index_t)comm->c_my_rank) {
        COLL_UCX_ERROR("mca_common_ucx_resolve_address(rank=%lu)"
                       "shouldn't be called on its own rank (loopback)", rank);
        return 1;
    }

    /* Check the cache for a previously established connection to that rank */
    ompi_proc_t *proc_peer = ompi_comm_peer_lookup(comm, rank);
    *addr = proc_peer->proc_endpoints[OMPI_PROC_ENDPOINT_TAG_COLL];
    *addr_len = 0; /* UCX doesn't need the length to unpack the address */
    if (*addr) {
       return 0;
    }

    /* Obtain the UCP address of the remote */
    int ret = opal_common_ucx_recv_worker_address(fisrt_version, &proc->super.proc_name, addr, addr_len);
    if (ret < 0) {
        COLL_UCX_ERROR("mca_common_ucx_recv_worker_address(proc=%d rank=%lu) failed",
                       proc_peer->super.proc_name.vpid, rank);
        return 1;
    }


    COLL_UCX_VERBOSE(2, "Got proc %d address, size %ld",
                     proc->super.proc_name.vpid, *addrlen_p);

    /* Cache the connection for future invocations with this rank */
    proc_peer->proc_endpoints[OMPI_PROC_ENDPOINT_TAG_COLL] = *addr;
    return 0;
}

static void mca_common_ucx_release_address(ucp_address_t *addr)
{
    /* no need to free - the address is stored in proc_peer->proc_endpoints */
}

int mca_common_ucx_neighbors_count(ompi_communicator_t *comm,
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

int mca_common_ucx_neighbors_query(ompi_communicator_t *comm,
                                   int *sources, int *destinations)
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
            sources[i] = destinations[i] = rpeer; i++;
            sources[i] = destinations[i] = speer; i++;
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

int mca_common_ucx_open(const char *prefix, size_t *request_size)
{
    struct ucp_params_t ucp_params;

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
    ucp_params.request_init      = mca_pml_ucx_request_init;
    ucp_params.request_cleanup   = mca_pml_ucx_request_cleanup;
    ucp_params.tag_sender_mask   = PML_UCX_SPECIFIC_SOURCE_MASK;
    ucp_params.mt_workers_shared = 0; /* we do not need mt support for context
                                     since it will be protected by worker */

#ifdef HAVE_DECL_UCP_PARAM_FIELD_ESTIMATED_NUM_PPN
    ucp_params.estimated_num_ppn = opal_process_info.num_local_peers + 1;
    ucp_params.field_mask       |= UCP_PARAM_FIELD_ESTIMATED_NUM_PPN;
#endif

#ifdef HAVE_UCG
    struct ucg_params_t ucg_params;

    ucp_params.field_mask              |= UCP_PARAM_FIELD_GROUP_PEER_INFO;
    ucp_params.features                |= UCP_FEATURE_GROUPS;
    ucp_params.peer_info.num_local      = ompi_process_info.num_local_peers + 1;
    ucp_params.peer_info.local_idx      = ompi_process_info.my_local_rank;
    ucp_params.peer_info.num_global     = ompi_process_info.num_procs;
    ucp_params.peer_info.global_idx     = ompi_process_info.myprocid.rank;

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
                                          mca_common_ucx_resolve_address;
    ucg_params.address.release_f        = (typeof(ucg_params.address.release_f))
                                          mca_common_ucx_release_address;
    ucg_params.neighbors.vertex_count_f = (typeof(ucg_params.neighbors.vertex_count_f))
                                          mca_common_ucx_neighbors_count;
    ucg_params.neighbors.vertex_query_f = (typeof(ucg_params.neighbors.vertex_query_f))
                                          mca_common_ucx_neighbors_query;
    ucg_params.mpi_reduce_f             = (typeof(ucg_params.mpi_reduce_f))
                                          ompi_op_reduce;
    ucg_params.mpi_in_place             = (void*)MPI_IN_PLACE;
    ucg_params.type_info.mpi_is_int_f   = (typeof(ucg_params.type_info.mpi_is_int_f))
                                          mca_common_ucx_is_dtype_int;
    ucg_params.type_info.mpi_is_fp_f    = (typeof(ucg_params.type_info.mpi_is_fp_f))
                                          mca_common_ucx_is_dtype_fp;
    ucg_params.type_info.mpi_is_sum_f   = (typeof(ucg_params.type_info.mpi_is_sum_f))
                                          mca_common_ucx_is_op_sum;
    ucg_params.type_info.mpi_is_loc_f   = (typeof(ucg_params.type_info.mpi_is_loc_f))
                                          mca_common_ucx_is_op_loc;
    ucg_params.fault.mode               = UCG_FAULT_IS_FATAL;

    return opal_common_ucx_open(prefix, ucg_params,
#else
    return opal_common_ucx_open(prefix, ucp_params,
#endif
                                mca_common_ucx_datatype_ctx_t_class,
                                request_size);
}

int mca_common_ucx_close(void)
{
    return opal_common_ucx_close();
}

int mca_common_ucx_init(mca_base_component_t *version,
                        mca_common_ucx_datatype_ctx_t **datatype_ctx)
{
    int rc = opal_common_ucx_init(ompi_mpi_thread_multiple, &version);
    if (rc < 0) {
        return rc;
    }

    mca_common_comp.address_version = version;

    return OMPI_SUCCESS;
}

int mca_common_ucx_cleanup(void)
{
    return opal_common_ucx_cleanup();
}
