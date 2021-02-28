/**
  Copyright (c) 2011      Mellanox Technologies. All rights reserved.
  Copyright (c) 2015      Research Organization for Information Science
                          and Technology (RIST). All rights reserved.
  Copyright (c) 2020      Huawei Technologies Co., Ltd. All rights reserved.
  $COPYRIGHT$

  Additional copyrights may follow

  $HEADER$
 */

#ifndef COLL_UCC_H
#define COLL_UCC_H

#include <ucc/api/ucc.h>

#include "ompi_config.h"
#include "ompi/mca/coll/coll.h"
#include "ompi/communicator/communicator.h"
#include "ompi/mca/common/ucx/common_ucx.h"

#define COLL_UCC_ASSERT  MCA_COMMON_UCX_ASSERT
#define COLL_UCC_ERROR   MCA_COMMON_UCX_ERROR
#define COLL_UCC_VERBOSE MCA_COMMON_UCX_VERBOSE

BEGIN_C_DECLS

typedef struct coll_ucc_persistent_request mca_coll_ucc_persistent_request_t;

typedef struct mca_coll_ucc_component {
    /* base MCA collectives component */
    mca_coll_base_component_t      super;

    /* MCA parameters */
    int                            priority;

    /* UCC per-rank context(s) */
    ucc_lib_h                      ucc_lib;
    ucc_context_h                  ucc_context;
} mca_coll_ucc_component_t;
OMPI_MODULE_DECLSPEC extern mca_coll_ucc_component_t mca_coll_ucc_component;

typedef struct mca_coll_ucc_module {
    mca_coll_base_module_t      super;

    /* UCC per-communicator context */
    ucc_team_h                  ucc_team;
} mca_coll_ucc_module_t;
OBJ_CLASS_DECLARATION(mca_coll_ucc_module_t);

/*
 * The collective operations themselves.
 */
int mca_coll_ucc_barrier(struct ompi_communicator_t *comm,
                         mca_coll_base_module_t *module);

int mca_coll_ucc_bcast(void *buff, int count, struct ompi_datatype_t *datatype,
                       int root, struct ompi_communicator_t *comm,
                       mca_coll_base_module_t *module);

int mca_coll_ucc_reduce(const void *sbuf, void* rbuf, int count,
                        struct ompi_datatype_t *dtype, struct ompi_op_t *op,
                        int root, struct ompi_communicator_t *comm,
                        mca_coll_base_module_t *module);

int mca_coll_ucc_allreduce(const void *sbuf, void *rbuf, int count,
                           struct ompi_datatype_t *dtype,
                           struct ompi_op_t *op,
                           struct ompi_communicator_t *comm,
                           mca_coll_base_module_t *module);

END_C_DECLS

#endif /* COLL_UCC_H_ */
