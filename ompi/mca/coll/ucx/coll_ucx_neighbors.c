/*
 * Copyright (c) 2006 The Trustees of Indiana University and Indiana University
 *                    Research and Technology Corporation.  All rights reserved.
 * Copyright (c) 2006 The Technical University of Chemnitz. All rights reserved.
 * Copyright (c) 2015 Los Alamos National Security, LLC.  All rights reserved.
 * Copyright (c) 2020 Huawei Technologies Co., Ltd. All rights reserved.
 * $COPYRIGHT$
 *
 * Original author: Torsten Hoefler <htor@cs.indiana.edu>
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "coll_ucx.h"
#include "ompi/mca/topo/base/base.h"

int mca_coll_ucx_neighbors_count(ompi_communicator_t *comm,
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

int mca_coll_ucx_neighbors_query(ompi_communicator_t *comm,
        int *sources, int *destinations)
{
    int res, indeg, outdeg;

    res = mca_coll_ucx_neighbors_count(comm, &indeg, &outdeg);
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
