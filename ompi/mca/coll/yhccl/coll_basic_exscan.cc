/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2014 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2011      NVIDIA Corporation.  All rights reserved.
 * Copyright (c) 2015-2018 Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
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
#include "ompi/constants.h"
#include "ompi/op/op.h"
#include "ompi/datatype/ompi_datatype.h"
#include "ompi/mca/pml/pml.h"
#include "ompi/mca/coll/coll.h"
#include "ompi/mca/coll/base/coll_tags.h"
#include "coll_yhccl.h"

#include <iostream>

int
mca_coll_yhccl_exscan_test(const void *sbuf, void *rbuf, size_t count,
                            struct ompi_datatype_t *dtype,
                            struct ompi_op_t *op,
                            struct ompi_communicator_t *comm,
                            mca_coll_base_module_t *module)
{
    std::cout << "std::cout  mca_coll_yhccl_exscan **** test" << std::endl;
    return OMPI_SUCCESS;
}

/*
 *	exscan_intra
 *
 *	Function:	- yhccl exscan operation
 *	Accepts:	- same arguments as MPI_Exscan()
 *	Returns:	- MPI_SUCCESS or error code
 */
int
mca_coll_yhccl_exscan_intra(const void *sbuf, void *rbuf, size_t count,
                            struct ompi_datatype_t *dtype,
                            struct ompi_op_t *op,
                            struct ompi_communicator_t *comm,
                            mca_coll_base_module_t *module)
{
    return ompi_coll_base_exscan_intra_linear(sbuf, rbuf, count, dtype, op, comm, module);
}


/*
 *	exscan_inter
 *
 *	Function:	- yhccl exscan operation
 *	Accepts:	- same arguments as MPI_Exscan()
 *	Returns:	- MPI_SUCCESS or error code
 */
int
mca_coll_yhccl_exscan_inter(const void *sbuf, void *rbuf, size_t count,
                            struct ompi_datatype_t *dtype,
                            struct ompi_op_t *op,
                            struct ompi_communicator_t *comm,
                            mca_coll_base_module_t *module)
{
    return OMPI_ERR_NOT_IMPLEMENTED;
}
