/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "mpiimpl.h"

/*
=== BEGIN_MPI_T_CVAR_INFO_BLOCK ===

cvars:
    - name        : MPIR_CVAR_INEIGHBOR_ALLGATHER_INTRA_ALGORITHM
      category    : COLLECTIVE
      type        : enum
      default     : auto
      class       : none
      verbosity   : MPI_T_VERBOSITY_USER_BASIC
      scope       : MPI_T_SCOPE_ALL_EQ
      description : |-
        Variable to select ineighbor_allgather algorithm
        auto - Internal algorithm selection (can be overridden with MPIR_CVAR_COLL_SELECTION_TUNING_JSON_FILE)
        sched_auto - Internal algorithm selection for sched-based algorithms
        sched_linear    - Force linear algorithm
        gentran_linear  - Force generic transport based linear algorithm

    - name        : MPIR_CVAR_INEIGHBOR_ALLGATHER_INTER_ALGORITHM
      category    : COLLECTIVE
      type        : enum
      default     : auto
      class       : none
      verbosity   : MPI_T_VERBOSITY_USER_BASIC
      scope       : MPI_T_SCOPE_ALL_EQ
      description : |-
        Variable to select ineighbor_allgather algorithm
        auto - Internal algorithm selection (can be overridden with MPIR_CVAR_COLL_SELECTION_TUNING_JSON_FILE)
        sched_auto - Internal algorithm selection for sched-based algorithms
        sched_linear    - Force linear algorithm
        gentran_linear  - Force generic transport based linear algorithm

    - name        : MPIR_CVAR_INEIGHBOR_ALLGATHER_DEVICE_COLLECTIVE
      category    : COLLECTIVE
      type        : boolean
      default     : true
      class       : none
      verbosity   : MPI_T_VERBOSITY_USER_BASIC
      scope       : MPI_T_SCOPE_ALL_EQ
      description : >-
        This CVAR is only used when MPIR_CVAR_DEVICE_COLLECTIVES
        is set to "percoll".  If set to true, MPI_Ineighbor_allgather will
        allow the device to override the MPIR-level collective
        algorithms.  The device might still call the MPIR-level
        algorithms manually.  If set to false, the device-override
        will be disabled.

=== END_MPI_T_CVAR_INFO_BLOCK ===
*/

int MPIR_Ineighbor_allgather_allcomm_auto(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                                          void *recvbuf, int recvcount, MPI_Datatype recvtype,
                                          MPIR_Comm * comm_ptr, MPIR_Request ** request)
{
    int mpi_errno = MPI_SUCCESS;

    MPIR_Csel_coll_sig_s coll_sig = {
        .coll_type = MPIR_CSEL_COLL_TYPE__INEIGHBOR_ALLGATHER,
        .comm_ptr = comm_ptr,

        .u.ineighbor_allgather.sendbuf = sendbuf,
        .u.ineighbor_allgather.sendcount = sendcount,
        .u.ineighbor_allgather.sendtype = sendtype,
        .u.ineighbor_allgather.recvbuf = recvbuf,
        .u.ineighbor_allgather.recvcount = recvcount,
        .u.ineighbor_allgather.recvtype = recvtype,
    };

    MPII_Csel_container_s *cnt = MPIR_Csel_search(comm_ptr->csel_comm, coll_sig);
    MPIR_Assert(cnt);

    switch (cnt->id) {
        case MPII_CSEL_CONTAINER_TYPE__ALGORITHM__MPIR_Ineighbor_allgather_allcomm_gentran_linear:
            mpi_errno =
                MPIR_Ineighbor_allgather_allcomm_gentran_linear(sendbuf, sendcount, sendtype,
                                                                recvbuf, recvcount, recvtype,
                                                                comm_ptr, request);
            break;

        case MPII_CSEL_CONTAINER_TYPE__ALGORITHM__MPIR_Ineighbor_allgather_intra_sched_auto:
            MPII_SCHED_WRAPPER(MPIR_Ineighbor_allgather_intra_sched_auto, comm_ptr, request,
                               sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype);
            break;

        case MPII_CSEL_CONTAINER_TYPE__ALGORITHM__MPIR_Ineighbor_allgather_inter_sched_auto:
            MPII_SCHED_WRAPPER(MPIR_Ineighbor_allgather_inter_sched_auto, comm_ptr, request,
                               sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype);
            break;

        case MPII_CSEL_CONTAINER_TYPE__ALGORITHM__MPIR_Ineighbor_allgather_allcomm_sched_linear:
            MPII_SCHED_WRAPPER(MPIR_Ineighbor_allgather_allcomm_sched_linear, comm_ptr, request,
                               sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype);
            break;

        default:
            MPIR_Assert(0);
    }

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

int MPIR_Ineighbor_allgather_intra_sched_auto(const void *sendbuf, int sendcount,
                                              MPI_Datatype sendtype, void *recvbuf, int recvcount,
                                              MPI_Datatype recvtype, MPIR_Comm * comm_ptr,
                                              MPIR_Sched_t s)
{
    int mpi_errno = MPI_SUCCESS;

    mpi_errno = MPIR_Ineighbor_allgather_allcomm_sched_linear(sendbuf, sendcount, sendtype,
                                                              recvbuf, recvcount, recvtype,
                                                              comm_ptr, s);
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    return mpi_errno;

  fn_fail:
    goto fn_exit;
}

int MPIR_Ineighbor_allgather_inter_sched_auto(const void *sendbuf, int sendcount,
                                              MPI_Datatype sendtype, void *recvbuf, int recvcount,
                                              MPI_Datatype recvtype, MPIR_Comm * comm_ptr,
                                              MPIR_Sched_t s)
{
    int mpi_errno = MPI_SUCCESS;

    mpi_errno = MPIR_Ineighbor_allgather_allcomm_sched_linear(sendbuf, sendcount, sendtype,
                                                              recvbuf, recvcount, recvtype,
                                                              comm_ptr, s);
    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    return mpi_errno;

  fn_fail:
    goto fn_exit;
}

int MPIR_Ineighbor_allgather_sched_auto(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                                        void *recvbuf, int recvcount, MPI_Datatype recvtype,
                                        MPIR_Comm * comm_ptr, MPIR_Sched_t s)
{
    int mpi_errno = MPI_SUCCESS;

    if (comm_ptr->comm_kind == MPIR_COMM_KIND__INTRACOMM) {
        mpi_errno = MPIR_Ineighbor_allgather_intra_sched_auto(sendbuf, sendcount, sendtype,
                                                              recvbuf, recvcount, recvtype,
                                                              comm_ptr, s);
    } else {
        mpi_errno = MPIR_Ineighbor_allgather_inter_sched_auto(sendbuf, sendcount, sendtype,
                                                              recvbuf, recvcount, recvtype,
                                                              comm_ptr, s);
    }

    return mpi_errno;
}

int MPIR_Ineighbor_allgather_impl(const void *sendbuf, int sendcount,
                                  MPI_Datatype sendtype, void *recvbuf,
                                  int recvcount, MPI_Datatype recvtype,
                                  MPIR_Comm * comm_ptr, MPIR_Request ** request)
{
    int mpi_errno = MPI_SUCCESS;

    *request = NULL;
    /* If the user picks one of the transport-enabled algorithms, branch there
     * before going down to the MPIR_Sched-based algorithms. */
    /* TODO - Eventually the intention is to replace all of the
     * MPIR_Sched-based algorithms with transport-enabled algorithms, but that
     * will require sufficient performance testing and replacement algorithms. */
    if (comm_ptr->comm_kind == MPIR_COMM_KIND__INTRACOMM) {
        switch (MPIR_CVAR_INEIGHBOR_ALLGATHER_INTRA_ALGORITHM) {
            case MPIR_CVAR_INEIGHBOR_ALLGATHER_INTRA_ALGORITHM_gentran_linear:
                mpi_errno =
                    MPIR_Ineighbor_allgather_allcomm_gentran_linear(sendbuf, sendcount, sendtype,
                                                                    recvbuf, recvcount, recvtype,
                                                                    comm_ptr, request);
                break;

            case MPIR_CVAR_INEIGHBOR_ALLGATHER_INTRA_ALGORITHM_sched_linear:
                MPII_SCHED_WRAPPER(MPIR_Ineighbor_allgather_allcomm_sched_linear, comm_ptr, request,
                                   sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype);
                break;

            case MPIR_CVAR_INEIGHBOR_ALLGATHER_INTRA_ALGORITHM_sched_auto:
                MPII_SCHED_WRAPPER(MPIR_Ineighbor_allgather_intra_sched_auto, comm_ptr, request,
                                   sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype);
                break;

            case MPIR_CVAR_INEIGHBOR_ALLGATHER_INTRA_ALGORITHM_auto:
                mpi_errno =
                    MPIR_Ineighbor_allgather_allcomm_auto(sendbuf, sendcount, sendtype, recvbuf,
                                                          recvcount, recvtype, comm_ptr, request);
                break;

            default:
                MPIR_Assert(0);
        }
    } else {
        switch (MPIR_CVAR_INEIGHBOR_ALLGATHER_INTER_ALGORITHM) {
            case MPIR_CVAR_INEIGHBOR_ALLGATHER_INTER_ALGORITHM_gentran_linear:
                mpi_errno =
                    MPIR_Ineighbor_allgather_allcomm_gentran_linear(sendbuf, sendcount, sendtype,
                                                                    recvbuf, recvcount, recvtype,
                                                                    comm_ptr, request);
                break;

            case MPIR_CVAR_INEIGHBOR_ALLGATHER_INTER_ALGORITHM_sched_linear:
                MPII_SCHED_WRAPPER(MPIR_Ineighbor_allgather_allcomm_sched_linear, comm_ptr, request,
                                   sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype);
                break;

            case MPIR_CVAR_INEIGHBOR_ALLGATHER_INTER_ALGORITHM_sched_auto:
                MPII_SCHED_WRAPPER(MPIR_Ineighbor_allgather_inter_sched_auto, comm_ptr, request,
                                   sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype);
                break;

            case MPIR_CVAR_INEIGHBOR_ALLGATHER_INTER_ALGORITHM_auto:
                mpi_errno =
                    MPIR_Ineighbor_allgather_allcomm_auto(sendbuf, sendcount, sendtype, recvbuf,
                                                          recvcount, recvtype, comm_ptr, request);
                break;

            default:
                MPIR_Assert(0);
        }
    }

    MPIR_ERR_CHECK(mpi_errno);

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

int MPIR_Ineighbor_allgather(const void *sendbuf, int sendcount,
                             MPI_Datatype sendtype, void *recvbuf,
                             int recvcount, MPI_Datatype recvtype,
                             MPIR_Comm * comm_ptr, MPIR_Request ** request)
{
    int mpi_errno = MPI_SUCCESS;

    if ((MPIR_CVAR_DEVICE_COLLECTIVES == MPIR_CVAR_DEVICE_COLLECTIVES_all) ||
        ((MPIR_CVAR_DEVICE_COLLECTIVES == MPIR_CVAR_DEVICE_COLLECTIVES_percoll) &&
         MPIR_CVAR_BARRIER_DEVICE_COLLECTIVE)) {
        mpi_errno =
            MPID_Ineighbor_allgather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype,
                                     comm_ptr, request);
    } else {
        mpi_errno = MPIR_Ineighbor_allgather_impl(sendbuf, sendcount, sendtype, recvbuf, recvcount,
                                                  recvtype, comm_ptr, request);
    }

    return mpi_errno;
}
