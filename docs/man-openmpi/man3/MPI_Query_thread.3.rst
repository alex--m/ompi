.. _mpi_query_thread:


MPI_Query_thread
================

.. include_body

:ref:`MPI_Query_thread` |mdash| Returns the current level of thread support

.. The following file was automatically generated
.. include:: ./bindings/mpi_query_thread.rst

OUTPUT PARAMETERS
-----------------
* ``provided``: C/Fortran only: Level of thread support (integer).
* ``ierror``: Fortran only: Error status (integer).

DESCRIPTION
-----------

This routine returns in *provided* the current level of thread support.
If MPI was initialized by a call to :ref:`MPI_Init_thread`, *provided* will
have the same value as was returned by that function.

The possible values of *provided* are as follows:

MPI_THREAD_SINGLE
   Only one thread may execute.

MPI_THREAD_FUNNELED
   If the process is multithreaded, only the thread that called
   MPI_Init[_thread] may make MPI calls.

MPI_THREAD_SERIALIZED
   If the process is multithreaded, only one thread may make MPI library
   calls at one time.

MPI_THREAD_MULTIPLE
   If the process is multithreaded, multiple threads may call MPI at
   once with no restrictions.


NOTES
-----

In Open MPI, *provided* is always MPI_THREAD_SINGLE, unless the program
has been linked with the multithreaded library, in which case *provided*
is MPI_THREAD_MULTIPLE.


ERRORS
------

.. include:: ./ERRORS.rst

.. seealso::
   * :ref:`MPI_Init`
   * :ref:`MPI_Init_thread`
