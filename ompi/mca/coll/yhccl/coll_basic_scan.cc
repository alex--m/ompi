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
#include "ompi/mca/coll/coll.h"
#include "ompi/mca/coll/base/coll_tags.h"
#include "ompi/mca/pml/pml.h"
#include "coll_yhccl.h"

#include <iostream>

int
mca_coll_yhccl_scan_test(const void *sbuf, void *rbuf, size_t count,
                          struct ompi_datatype_t *dtype,
                          struct ompi_op_t *op,
                          struct ompi_communicator_t *comm,
                          mca_coll_base_module_t *module)
{
    std::cout << "std::cout  mca_coll_yhccl_scan **** test" << std::endl;
    return OMPI_SUCCESS;
}

/*
 *	scan
 *
 *	Function:	- yhccl scan operation
 *	Accepts:	- same arguments as MPI_Scan()
 *	Returns:	- MPI_SUCCESS or error code
 */
int
mca_coll_yhccl_scan_intra(const void *sbuf, void *rbuf, size_t count,
                          struct ompi_datatype_t *dtype,
                          struct ompi_op_t *op,
                          struct ompi_communicator_t *comm,
                          mca_coll_base_module_t *module)
{
    return ompi_coll_base_scan_intra_linear(sbuf, rbuf, count, dtype, op, comm, module);
}
