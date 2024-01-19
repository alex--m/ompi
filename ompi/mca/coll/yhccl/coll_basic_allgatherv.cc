/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2017 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2012      Oak Ridge National Labs.  All rights reserved.
 * Copyright (c) 2015      Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ompi_config.h"
#undef OPAL_C_HAVE__GENERIC
#include "coll_yhccl.h"

#include "mpi.h"
#include "ompi/datatype/ompi_datatype.h"
#include "ompi/constants.h"
#include "ompi/mca/coll/coll.h"
#include "ompi/mca/coll/base/coll_tags.h"
#include "coll_yhccl.h"
#include <iostream>


int
mca_coll_yhccl_allgatherv_test(const void *sbuf, size_t scount,
                                struct ompi_datatype_t *sdtype,
                                void *rbuf, const int *rcounts, const int *disps,
                                struct ompi_datatype_t *rdtype,
                                struct ompi_communicator_t *comm,
                                mca_coll_base_module_t *module)
{
    std::cout << "std::cout  mca_coll_yhccl_allgatherv **** test" << std::endl;
    return OMPI_SUCCESS;
}



/*
 *	allgatherv_inter
 *
 *	Function:	- allgatherv using other MPI collectives
 *	Accepts:	- same as MPI_Allgatherv()
 *	Returns:	- MPI_SUCCESS or error code
 */
int
mca_coll_yhccl_allgatherv_inter(const void *sbuf, int scount,
                                struct ompi_datatype_t *sdtype,
                                void *rbuf, ompi_count_array_t rcounts,
                                ompi_disp_array_t displs,
                                struct ompi_datatype_t *rdtype,
                                struct ompi_communicator_t *comm,
                                mca_coll_base_module_t *module)
{
    int rsize, err, i;
    int *scounts, *sdisps;
    ompi_disp_array_t sdisps_arr;
    ompi_count_array_t scounts_arr;

    rsize = ompi_comm_remote_size(comm);

    scounts = (int *) malloc(2 * rsize * sizeof(int));
    OMPI_COUNT_ARRAY_INIT(&scounts_arr, scounts);

    sdisps = scounts + rsize;
    OMPI_DISP_ARRAY_INIT(&sdisps_arr, sdisps);

    if (NULL == scounts) {
        return OMPI_ERR_OUT_OF_RESOURCE;
    }

    for (i = 0; i < rsize; i++) {
        scounts[i] = scount;
        sdisps[i] = 0;
    }

    err = comm->c_coll->coll_alltoallv(sbuf, scounts_arr, sdisps_arr, sdtype,
                                       rbuf, rcounts, displs, rdtype, comm,
                                       comm->c_coll->coll_alltoallv_module);

    if (NULL != scounts) {
        free(scounts);
    }

    return err;
}
