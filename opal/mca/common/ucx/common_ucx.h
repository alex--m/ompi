/*
 * Copyright (c) 2018      Mellanox Technologies.  All rights reserved.
 *                         All rights reserved.
 * Copyright (c) 2018      Research Organization for Information Science
 *                         and Technology (RIST).  All rights reserved.
 * Copyright (c) 2019-2020 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2023      NVIDIA Corporation. All rights reserved.
 * Copyright (c) 2020      Huawei Technologies Co., Ltd.  All rights
 *                         reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef _COMMON_UCX_H_
#define _COMMON_UCX_H_

#include "opal_config.h"

#include <stdint.h>

#include <ucp/api/ucp.h>

#ifdef HAVE_UCG
#include <ucg/api/ucg.h>
#else
#define ucg_context_h void*
#define ucg_params_t void
#define ucg_request_get_progress (void*)
typedef unsigned (*ucg_collective_progress_t)(void*);
#endif

#include "opal/class/opal_list.h"
#include "opal/include/opal/constants.h"
#include "opal/mca/mca.h"
#include "opal/util/proc.h"
#include "opal/util/output.h"
#include "opal/runtime/opal_progress.h"
#include "opal/util/output.h"

BEGIN_C_DECLS

#define MCA_COMMON_UCX_ENABLE_DEBUG OPAL_ENABLE_DEBUG
#if MCA_COMMON_UCX_ENABLE_DEBUG
#    define MCA_COMMON_UCX_MAX_VERBOSE 100
#    define MCA_COMMON_UCX_ASSERT(_x)  assert(_x)
#else
#    define MCA_COMMON_UCX_MAX_VERBOSE 2
#    define MCA_COMMON_UCX_ASSERT(_x)
#endif

#define MCA_COMMON_UCX_PER_TARGET_OPS_THRESHOLD 1000
#define MCA_COMMON_UCX_GLOBAL_OPS_THRESHOLD     1000

#define UCX_VERSION(_major, _minor, _build) (((_major) *100) + (_minor))

#define _MCA_COMMON_UCX_QUOTE(_x) #_x
#define MCA_COMMON_UCX_QUOTE(_x)  _MCA_COMMON_UCX_QUOTE(_x)

#define MCA_COMMON_UCX_ERROR(...) MCA_COMMON_UCX_VERBOSE(0, " Error: " __VA_ARGS__)

#define MCA_COMMON_UCX_WARN(...) MCA_COMMON_UCX_VERBOSE(0, " Warning: " __VA_ARGS__)

#define MCA_COMMON_UCX_VERBOSE(_level, ...)                                                  \
    if (((_level) <= MCA_COMMON_UCX_MAX_VERBOSE) && ((_level) <= opal_common_ucx.verbose)) { \
        opal_output_verbose(_level, opal_common_ucx.output,                                  \
                            __FILE__ ":" MCA_COMMON_UCX_QUOTE(__LINE__) " " __VA_ARGS__);    \
    }

enum opal_common_ucx_req_type {
    OPAL_COMMON_UCX_REQUEST_TYPE_UCP = 0,
#ifdef HAVE_UCG
    OPAL_COMMON_UCX_REQUEST_TYPE_UCG = 1,
#endif
};

/* progress loop to allow call UCX/opal progress, while testing requests by type */
/* used C99 for-statement variable initialization */
#ifdef HAVE_UCG
static inline void opal_progress_by_req(enum opal_common_ucx_req_type req_type,
                                        ucp_worker_h worker, void *req_obj,
                                        void *info)
{
    switch (req_type) {
        case OPAL_COMMON_UCX_REQUEST_TYPE_UCG:
            ((ucg_collective_progress_t)info)(req_obj);
            break;

        default:
            ucp_worker_progress(worker);
            break;
    }
}

#define MCA_COMMON_UCX_PROGRESS_LOOP_BY_REQ(_worker, _type, _req, _info) \
    void *orig_info = _info; \
    unsigned iter_mask = opal_common_ucx.progress_iters_mask; \
    if (_type == OPAL_COMMON_UCX_REQUEST_TYPE_UCG) { \
        _info = (typeof(_info))ucg_collective_get_status_ptr(_req); \
        iter_mask = (iter_mask << 3) | ((1 << 3) - 1) ; \
    } \
    for (unsigned iter = 0;; (++iter & iter_mask) \
                                 ? opal_progress_by_req(_type, _worker,     \
                                                        _req, orig_info)    \
                                 : (void) opal_progress())
#else
#define MCA_COMMON_UCX_PROGRESS_LOOP_BY_REQ(_worker, _type, _req, _info) \
    for (unsigned iter = 0;; (++iter & opal_common_ucx.progress_iters_mask) \
                                 ? (void) ucp_worker_progress(_worker)      \
                                 : (void) opal_progress())
#endif

#define MCA_COMMON_UCX_PROGRESS_LOOP(_worker)                                 \
        void *dummy_info = NULL;                                              \
        MCA_COMMON_UCX_PROGRESS_LOOP_BY_REQ(_worker,                          \
                                            OPAL_COMMON_UCX_REQUEST_TYPE_UCP, \
                                            NULL, dummy_info)

#define MCA_COMMON_UCX_WAIT_LOOP(_request, _req_type, _worker, _msg, _info, _completed)  \
    do {                                                                                 \
        ucs_status_t status;                                                             \
        /* call UCX progress */                                                          \
        MCA_COMMON_UCX_PROGRESS_LOOP_BY_REQ(_worker, _req_type, _request, _info)         \
        {                                                                                \
            status = opal_common_ucx_request_status(_request, _req_type, _info);         \
            if (UCS_INPROGRESS != status) {                                              \
                _completed;                                                              \
                if (OPAL_LIKELY(UCS_OK == status)) {                                     \
                    return OPAL_SUCCESS;                                                 \
                } else {                                                                 \
                    MCA_COMMON_UCX_VERBOSE(1, "%s failed: %d, %s",                       \
                                           (_msg) ? (_msg) : __func__,                   \
                                           UCS_PTR_STATUS(_request),                     \
                                           ucs_status_string(UCS_PTR_STATUS(_request))); \
                    return OPAL_ERROR;                                                   \
                }                                                                        \
            }                                                                            \
        }                                                                                \
    } while (0)

typedef struct opal_common_ucx_module {
    int                         output;
    int                         verbose;
    unsigned                    progress_iters_mask;
    int                         registered;
    bool                        opal_mem_hooks;
    char                      **tls;
    char                      **devices;

    ucp_worker_h                ucp_worker;
    ucp_context_h               ucp_context;
#ifdef HAVE_UCG
    ucb_context_h               ucb_context;
    ucg_context_h               ucg_context;
#endif

    unsigned                    ref_count;
    const mca_base_component_t *first_version;
    bool                        request_leak_check;
    uint32_t                    op_attr_nonblocking;
} opal_common_ucx_module_t;

typedef struct opal_common_ucx_del_proc {
    ucp_ep_h ep;
    size_t vpid;
} opal_common_ucx_del_proc_t;

typedef enum {
    /* No supported transports found (according to configured list of supported
       transports) */
    OPAL_COMMON_UCX_SUPPORT_NONE,

    /* Have supported transports but not supported devices */
    OPAL_COMMON_UCX_SUPPORT_TRANSPORT,

    /* Have both supported transports and supported devices */
    OPAL_COMMON_UCX_SUPPORT_DEVICE,
} opal_common_ucx_support_level_t;

extern opal_common_ucx_module_t opal_common_ucx;

#ifdef HAVE_UCG
extern void *mca_common_ucx_collective_blocking;
#endif

OPAL_DECLSPEC void opal_common_ucx_mca_register(void);
OPAL_DECLSPEC void opal_common_ucx_mca_deregister(void);
OPAL_DECLSPEC opal_common_ucx_support_level_t opal_common_ucx_support_level(ucp_context_h context);
OPAL_DECLSPEC void opal_common_ucx_mca_proc_added(void);
OPAL_DECLSPEC void opal_common_ucx_empty_complete_cb(void *request, ucs_status_t status);
OPAL_DECLSPEC int opal_common_ucx_mca_pmix_fence(ucp_worker_h worker);
OPAL_DECLSPEC int opal_common_ucx_mca_pmix_fence_nb(int *fenced);
OPAL_DECLSPEC int opal_common_ucx_del_procs(opal_common_ucx_del_proc_t *procs, size_t count,
                                            size_t my_rank, size_t max_disconnect,
                                            ucp_worker_h worker);
OPAL_DECLSPEC int opal_common_ucx_del_procs_nofence(opal_common_ucx_del_proc_t *procs, size_t count,
                                                    size_t my_rank, size_t max_disconnect,
                                                    ucp_worker_h worker);
OPAL_DECLSPEC void opal_common_ucx_mca_var_register(const mca_base_component_t *component);
OPAL_DECLSPEC ucs_thread_mode_t opal_common_ucx_thread_mode(int ompi_mode);


int opal_common_ucx_open(const char *prefix,
#ifdef HAVE_UCG
                         const ucg_params_t *ucg_params,
#else
                         const ucp_params_t *ucp_params,
#endif
                         size_t *request_size);
int opal_common_ucx_close(void);
int opal_common_ucx_init(int enable_mpi_threads,
                         const mca_base_component_t *version);
int opal_common_ucx_cleanup(void);
int opal_common_ucx_recv_worker_address(opal_process_name_t *proc_name,
                                        ucp_address_t **address_p,
                                        size_t *addrlen_p);

/**
 * Load an integer value of \c size bytes from \c ptr and cast it to uint64_t.
 */
static inline uint64_t opal_common_ucx_load_uint64(const void *ptr, size_t size)
{
    if (sizeof(uint8_t) == size) {
        return *(uint8_t *) ptr;
    } else if (sizeof(uint16_t) == size) {
        return *(uint16_t *) ptr;
    } else if (sizeof(uint32_t) == size) {
        return *(uint32_t *) ptr;
    } else {
        return *(uint64_t *) ptr;
    }
}

/**
 * Cast and store a uint64_t value to a value of \c size bytes pointed to by \c ptr.
 */
static inline void opal_common_ucx_store_uint64(uint64_t value, void *ptr, size_t size)
{
    if (sizeof(uint8_t) == size) {
        *(uint8_t *) ptr = value;
    } else if (sizeof(uint16_t) == size) {
        *(uint16_t *) ptr = value;
    } else if (sizeof(uint32_t) == size) {
        *(uint32_t *) ptr = value;
    } else {
        *(uint64_t *) ptr = value;
    }
}

static inline
ucs_status_t opal_common_ucx_request_status(ucs_status_ptr_t request,
                                            enum opal_common_ucx_req_type type,
                                            void *info)
{
    switch (type) {
    case OPAL_COMMON_UCX_REQUEST_TYPE_UCP:
#if !HAVE_DECL_UCP_REQUEST_CHECK_STATUS
        return ucp_request_test(request, (ucp_tag_recv_info_t*)info);
#else
        return ucp_request_check_status(request);
#endif

#ifdef HAVE_UCG
    case OPAL_COMMON_UCX_REQUEST_TYPE_UCG:
        return *(volatile ucs_status_t*)info;
#endif

    default:
        break;
    }
    return OPAL_ERROR;
}

static inline
int opal_common_ucx_wait_request(ucs_status_ptr_t request, ucp_worker_h worker,
                                 enum opal_common_ucx_req_type type,
                                 const char *msg)
{
    void *info = NULL;

    /* check for request completed or failed */
    if (OPAL_LIKELY(UCS_OK == request)) {
        return OPAL_SUCCESS;
    } else if (OPAL_UNLIKELY(UCS_PTR_IS_ERR(request))) {
        MCA_COMMON_UCX_VERBOSE(1, "%s failed: %d, %s", msg ? msg : __func__,
                               UCS_PTR_STATUS(request), ucs_status_string(UCS_PTR_STATUS(request)));
        return OPAL_ERROR;
    }

    MCA_COMMON_UCX_WAIT_LOOP(request, type, worker, msg, info, ucp_request_free(request));
}

static inline int opal_common_ucx_ep_flush(ucp_ep_h ep, ucp_worker_h worker)
{
#if HAVE_DECL_UCP_EP_FLUSH_NB
    ucs_status_ptr_t request;

    request = ucp_ep_flush_nb(ep, 0, opal_common_ucx_empty_complete_cb);
    return opal_common_ucx_wait_request(request, worker,
                                        OPAL_COMMON_UCX_REQUEST_TYPE_UCP,
                                        "ucp_ep_flush_nb");
#else
    ucs_status_t status;

    status = ucp_ep_flush(ep);
    return (status == UCS_OK) ? OPAL_SUCCESS : OPAL_ERROR;
#endif
}

static inline int opal_common_ucx_worker_flush(ucp_worker_h worker)
{
#if HAVE_DECL_UCP_WORKER_FLUSH_NB
    ucs_status_ptr_t request;

    request = ucp_worker_flush_nb(worker, 0, opal_common_ucx_empty_complete_cb);

    return opal_common_ucx_wait_request(request, worker,
                                        OPAL_COMMON_UCX_REQUEST_TYPE_UCP,
                                        "ucp_worker_flush_nb");
#else
    ucs_status_t status;

    status = ucp_worker_flush(worker);
    return (status == UCS_OK) ? OPAL_SUCCESS : OPAL_ERROR;
#endif
}

static inline int opal_common_ucx_atomic_fetch(ucp_ep_h ep, ucp_atomic_fetch_op_t opcode,
                                               uint64_t value, void *result, size_t op_size,
                                               uint64_t remote_addr, ucp_rkey_h rkey,
                                               ucp_worker_h worker)
{
    ucs_status_ptr_t request;

    request = ucp_atomic_fetch_nb(ep, opcode, value, result,
                                  op_size, remote_addr, rkey,
                                  opal_common_ucx_empty_complete_cb);

    return opal_common_ucx_wait_request(request, worker,
                                        OPAL_COMMON_UCX_REQUEST_TYPE_UCP,
                                        "ucp_atomic_fetch_nb");
}

static inline ucs_status_ptr_t
opal_common_ucx_atomic_fetch_nb(ucp_ep_h ep, ucp_atomic_fetch_op_t opcode, uint64_t value,
                                void *result, size_t op_size, uint64_t remote_addr, ucp_rkey_h rkey,
                                ucp_send_callback_t req_handler, ucp_worker_h worker)
{
    return ucp_atomic_fetch_nb(ep, opcode, value, result, op_size, remote_addr, rkey, req_handler);
}

static inline int opal_common_ucx_atomic_cswap(ucp_ep_h ep, uint64_t compare, uint64_t value,
                                               void *result, size_t op_size, uint64_t remote_addr,
                                               ucp_rkey_h rkey, ucp_worker_h worker)
{
    opal_common_ucx_store_uint64(value, result, op_size);

    return opal_common_ucx_atomic_fetch(ep, UCP_ATOMIC_FETCH_OP_CSWAP, compare, result, op_size,
                                        remote_addr, rkey, worker);
}

static inline ucs_status_ptr_t
opal_common_ucx_atomic_cswap_nb(ucp_ep_h ep, uint64_t compare, uint64_t value, void *result,
                                size_t op_size, uint64_t remote_addr, ucp_rkey_h rkey,
                                ucp_send_callback_t req_handler, ucp_worker_h worker)
{
    opal_common_ucx_store_uint64(value, result, op_size);

    return opal_common_ucx_atomic_fetch_nb(ep, UCP_ATOMIC_FETCH_OP_CSWAP, compare, result, op_size,
                                           remote_addr, rkey, req_handler, worker);
}

END_C_DECLS

#endif
