.. _mpi_comm_dup_with_info:

MPI_Comm_dup_with_info
======================

.. include_body

:ref:`MPI_Comm_dup_with_info` |mdash| Duplicates an existing communicator using
provided info.

.. The following file was automatically generated
.. include:: ./bindings/mpi_comm_dup_with_info.rst

INPUT PARAMETER
---------------

* ``comm`` : Communicator (handle).
* ``info`` : Info argument (handle).

OUTPUT PARAMETERS
-----------------

* ``newcomm`` : Copy of comm (handle).
* ``ierror`` : Fortran only: Error status (integer).

DESCRIPTION
-----------

:ref:`MPI_Comm_dup_with_info` acts exactly like :ref:`MPI_Comm_dup` except that the
info hints associated with the communicator comm are not duplicated in
newcomm. The hints provided by the argument info are associated with the
output communicator newcomm instead. See :ref:`MPI_Comm_set_info` for the
list of recognized info keys.

NOTES
-----

This operation is used to provide a parallel library call with a
duplicate communication space that has the same properties as the
original communicator. This includes any attributes (see below) and
topologies (see the "Process Topologies" chapter in the `MPI Standard
<https://www.mpi-forum.org/docs/>`_.
This call is valid even if there are pending point-to-point
communications involving the communicator comm. A typical call might
involve an :ref:`MPI_Comm_dup_with_info` at the beginning of the parallel call,
and an :ref:`MPI_Comm_free` of that duplicated communicator at the end of the
call. Other models of communicator management are also possible. This
call applies to both intra- and intercommunicators. Note that it is not
defined by the MPI standard what happens if the attribute copy callback
invokes other MPI functions. In Open MPI, it is not valid for attribute
copy callbacks (or any of their children) to add or delete attributes on
the same object on which the attribute copy callback is being invoked.

ERRORS
------

.. include:: ./ERRORS.rst

.. seealso:: :ref:`MPI_Comm_dup`
