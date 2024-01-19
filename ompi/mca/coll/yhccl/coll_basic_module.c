/* -*- Mode: C; c-yhccl-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2016 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2012      Sandia National Laboratories. All rights reserved.
 * Copyright (c) 2013      Los Alamos National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2015      Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * Copyright (c) 2016-2017 IBM Corporation.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ompi_config.h"
#include "coll_yhccl.h"

#include <stdio.h>

#include "mpi.h"
#include "ompi/mca/coll/coll.h"
#include "ompi/mca/coll/base/base.h"
#include "coll_yhccl.h"

/*
 * Initial query function that is invoked during MPI_INIT, allowing
 * this component to disqualify itself if it doesn't support the
 * required level of thread support.
 */
int mca_coll_yhccl_init_query(bool enable_progress_threads,
                              bool enable_mpi_threads)
{
    /* Nothing to do */
    return OMPI_SUCCESS;
}

/*
 * Invoked when there's a new communicator that has been created.
 * Look at the communicator and decide which set of functions and
 * priority we want to return.
 */
mca_coll_base_module_t *
mca_coll_yhccl_comm_query(struct ompi_communicator_t *comm,
                          int *priority)
{
    mca_coll_yhccl_module_t *yhccl_module = OBJ_NEW(mca_coll_yhccl_module_t);
    if (NULL == yhccl_module)
        return NULL;

    *priority = mca_coll_yhccl_priority;

    /* Choose whether to use [intra|inter], and [linear|log]-based
     * algorithms. */
    yhccl_module->super.coll_module_enable  = mca_coll_yhccl_module_enable;
    yhccl_module->super.coll_module_disable = mca_coll_yhccl_module_disable;

    return &(yhccl_module->super);
}

#define BASIC_INSTALL_COLL_API(__comm, __module, __api, __coll)                 \
    do                                                                          \
    {                                                                           \
        (__module)->coll_##__api = __coll;                                \
        MCA_COLL_INSTALL_API(__comm, __api, __coll, __module, "yhccl"); \
    } while (0)

#define BASIC_UNINSTALL_COLL_API(__comm, __module, __api)                 \
    do                                                                    \
    {                                                                     \
        if (__comm->c_coll->coll_##__api##_module == __module ) { \
            MCA_COLL_INSTALL_API(__comm, __api, NULL, NULL, "yhccl");     \
        }                                                                 \
    } while (0)

/*
 * Init module on the communicator
 */
int mca_coll_yhccl_module_enable(mca_coll_base_module_t *module,
                                 struct ompi_communicator_t *comm)
{
    /* prepare the placeholder for the array of request* */
    module->base_data = OBJ_NEW(mca_coll_base_comm_t);

    BASIC_INSTALL_COLL_API(comm, module, reduce, mca_coll_yhccl_pjt_reduce_global);
    BASIC_INSTALL_COLL_API(comm, module, allreduce, mca_coll_yhccl_pjt_allreduce_global);

    return module->base_data ? OMPI_SUCCESS : OMPI_ERROR;
}

int mca_coll_yhccl_module_disable(mca_coll_base_module_t *module,
                                 struct ompi_communicator_t *comm)
{
    /* prepare the placeholder for the array of request* */
    module->base_data = OBJ_NEW(mca_coll_base_comm_t);

    BASIC_UNINSTALL_COLL_API(comm, module, reduce);
    BASIC_UNINSTALL_COLL_API(comm, module, allreduce);

    return module->base_data ? OMPI_SUCCESS : OMPI_ERROR;
}