/**
  Copyright (c) 2011      Mellanox Technologies. All rights reserved.
  Copyright (c) 2015      Research Organization for Information Science
                          and Technology (RIST). All rights reserved.
  Copyright (c) 2019      Huawei Technologies Co., Ltd. All rights reserved.
  $COPYRIGHT$

  Additional copyrights may follow

  $HEADER$
 */

#ifndef COLL_UCX_H
#define COLL_UCX_H

#include <ucg/api/ucg_mpi.h>

#include "ompi_config.h"
#include "ompi/mca/coll/coll.h"
#include "ompi/communicator/communicator.h"
#include "ompi/mca/common/ucx/common_ucx.h"


#ifndef UCX_VERSION
#define UCX_VERSION(major, minor) (((major)<<UCX_MAJOR_BIT)|((minor)<<UCX_MINOR_BIT))
#endif

#define COLL_UCX_ASSERT  MCA_COMMON_UCX_ASSERT
#define COLL_UCX_ERROR   MCA_COMMON_UCX_ERROR
#define COLL_UCX_VERBOSE MCA_COMMON_UCX_VERBOSE

BEGIN_C_DECLS

typedef struct coll_ucx_persistent_request mca_coll_ucx_persistent_request_t;

typedef struct mca_coll_ucx_component {
    /* base MCA collectives component */
    mca_coll_base_component_t super;

    /* MCA parameters */
    int                       priority;
    bool                      stable_reduce;
    bool                      get_imbalance;
} mca_coll_ucx_component_t;
extern mca_coll_ucx_component_t mca_coll_ucx_component;

typedef struct mca_coll_ucx_module {
    mca_coll_base_module_t super;

    /* UCX per-communicator context */
    ucg_group_h            ucg_group;
    ucg_coll_h             barrier_init;
    ucg_coll_h             ibarrier_init;
} mca_coll_ucx_module_t;
OBJ_CLASS_DECLARATION(mca_coll_ucx_module_t);

struct coll_ucx_persistent_request {
    mca_common_ucx_persistent_request_t super;
    ucg_coll_h                          pcoll;
    ucg_collective_progress_t           progress_f;
};

/*
 * Component-oriented functions for using UCX collectives.
 */
int  mca_coll_ucx_open(void);
int  mca_coll_ucx_close(void);
int  mca_coll_ucx_init(void);
void mca_coll_ucx_cleanup(void);
int  mca_coll_ucx_progress(void);

/*
 * TESTING PURPOSES: get the worker from the module.
 */
ucp_worker_h mca_coll_ucx_get_component_worker(void);

/*
 * Start persistent collectives from an array of requests.
 */
int mca_coll_ucx_start(size_t count, ompi_request_t** requests);

/*
 * Obtain the address for a remote node.
 */
int mca_coll_ucx_resolve_address(void *cb_group_obj,
                                 ucg_group_member_index_t idx,
                                 ucp_address_t **addr, size_t *addr_len);

/*
 * Release an obtained address for a remote node.
 */
void mca_coll_ucx_release_address(ucp_address_t *addr);

int mca_coll_ucx_neighbors_count(ompi_communicator_t *comm,
                                 int *indegree, int *outdegree);

int mca_coll_ucx_neighbors_query(ompi_communicator_t *comm,
                                 int *sources, int *destinations);


/*
 * The collective operations themselves.
 */
int mca_coll_ucx_allgather(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_allgatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps,  struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_allreduce(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_allreduce_stable(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_alltoall(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void* rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_alltoallv(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_alltoallw(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_barrier(struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_bcast(void *buff, size_t count, struct ompi_datatype_t *datatype, int root,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_exscan(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_exscan_stable(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_gather(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_gatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *disps, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_reduce(const void *sbuf, void* rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_reduce_stable(const void *sbuf, void* rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_reduce_scatter(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_reduce_scatter_stable(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_reduce_scatter_block(const void *sbuf, void *rbuf, size_t rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_reduce_scatter_block_stable(const void *sbuf, void *rbuf, size_t rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_scan(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_scan_stable(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_scatter(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_scatterv(const void *sbuf, const int *scounts, const int *disps, struct ompi_datatype_t *sdtype,
   void* rbuf, int rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_iallgather(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_iallgatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps,  struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_iallreduce(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm,
   ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_iallreduce_stable(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm,
   ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_ialltoall(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void* rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_ialltoallv(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_ialltoallw(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_ibarrier(struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_ibcast(void *buff, size_t count, struct ompi_datatype_t *datatype, int root,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_iexscan(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_iexscan_stable(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_igather(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_igatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *disps, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_ireduce(const void *sbuf, void* rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_ireduce_stable(const void *sbuf, void* rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_ireduce_scatter(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_ireduce_scatter_stable(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_ireduce_scatter_block(const void *sbuf, void *rbuf, size_t rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_ireduce_scatter_block_stable(const void *sbuf, void *rbuf, size_t rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_iscan(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_iscan_stable(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_iscatter(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_iscatterv(const void *sbuf, const int *scounts, const int *disps, struct ompi_datatype_t *sdtype,
   void* rbuf, int rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_allgather_init(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_allgatherv_init(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps,  struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_allreduce_init(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info,
   ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_allreduce_stable_init(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info,
   ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_alltoall_init(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void* rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_alltoallv_init(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_alltoallw_init(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_barrier_init(struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_bcast_init(void *buff, size_t count, struct ompi_datatype_t *datatype, int root,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_exscan_init(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_exscan_stable_init(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_gather_init(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_gatherv_init(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *disps, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_reduce_init(const void *sbuf, void* rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_reduce_stable_init(const void *sbuf, void* rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_reduce_scatter_init(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_reduce_scatter_stable_init(const void *sbuf, void *rbuf, const int *rcounts, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_reduce_scatter_block_init(const void *sbuf, void *rbuf, size_t rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_reduce_scatter_block_stable_init(const void *sbuf, void *rbuf, size_t rcount, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_scan_init(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_scan_stable_init(const void *sbuf, void *rbuf, size_t count, struct ompi_datatype_t *dtype,
   struct ompi_op_t *op, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_scatter_init(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_scatterv_init(const void *sbuf, const int *scounts, const int *disps, struct ompi_datatype_t *sdtype,
   void* rbuf, int rcount, struct ompi_datatype_t *rdtype,
   int root, struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_neighbor_allgather(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_neighbor_allgatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps,  struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_neighbor_alltoall(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void* rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_neighbor_alltoallv(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_neighbor_alltoallw(const void *sbuf, const int *scounts, const MPI_Aint *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const MPI_Aint *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, mca_coll_base_module_t *module);

int mca_coll_ucx_ineighbor_allgather(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_ineighbor_allgatherv(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps,  struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_ineighbor_alltoall(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void* rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_ineighbor_alltoallv(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_ineighbor_alltoallw(const void *sbuf, const int *scounts, const MPI_Aint *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const MPI_Aint *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_neighbor_allgather_init(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void *rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_neighbor_allgatherv_init(const void *sbuf, int scount, struct ompi_datatype_t *sdtype,
   void * rbuf, const int *rcounts, const int *disps,  struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_neighbor_alltoall_init(const void *sbuf, size_t scount, struct ompi_datatype_t *sdtype,
   void* rbuf, size_t rcount, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_neighbor_alltoallv_init(const void *sbuf, const int *scounts, const int *sdisps, struct ompi_datatype_t *sdtype,
   void *rbuf, const int *rcounts, const int *rdisps, struct ompi_datatype_t *rdtype,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);

int mca_coll_ucx_neighbor_alltoallw_init(const void *sbuf, const int *scounts, const MPI_Aint *sdisps, struct ompi_datatype_t * const *sdtypes,
   void *rbuf, const int *rcounts, const MPI_Aint *rdisps, struct ompi_datatype_t * const *rdtypes,
   struct ompi_communicator_t *comm, struct ompi_info_t *info, ompi_request_t ** request, mca_coll_base_module_t *module);
END_C_DECLS

#endif /* COLL_UCX_H_ */
