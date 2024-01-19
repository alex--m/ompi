#ifndef PJT_INCLUDE
#define PJT_INCLUDE

#define PJT_MPI

// #define PAPI
// #define IPH_NUMA
#ifdef __AVX__
#define PJT_AVX_ASSEMBLY_MEMCPY
#endif
#ifdef PJT_MPI

#include "coll_yhccl.h"
#include "ompi/datatype/ompi_datatype.h"
#include "ompi/op/op.h"
#include "ompi_config.h"
#include "mpi.h"

#else

#include "mpi.h"

#endif
#endif