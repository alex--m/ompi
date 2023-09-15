/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (C) Mellanox Technologies Ltd. 2018. ALL RIGHTS RESERVED.
 * Copyright (c) 2019      Intel, Inc.  All rights reserved.
 * Copyright (c) 2019      Research Organization for Information Science
 *                         and Technology (RIST).  All rights reserved.
 * Copyright (c) 2021-2024 Triad National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2022      Google, LLC. All rights reserved.
 * Copyright (c) 2022      IBM Corporation.  All rights reserved.
 * Copyright (c) 2023      NVIDIA Corporation.  All rights reserved.
 * Copyright (c) 2020      Huawei Technologies Co., Ltd.  All rights
 *                         reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "opal_config.h"

#include "common_ucx.h"
#include "opal/mca/base/mca_base_framework.h"
#include "opal/mca/base/mca_base_var.h"
#include "opal/mca/pmix/pmix-internal.h"
#include "opal/memoryhooks/memory.h"
#include "opal/util/argv.h"
#include "opal/util/printf.h"
#include "opal/util/bit_ops.h"

#include "mpi.h"

#include <fnmatch.h>
#include <stdio.h>
#include <ucm/api/ucm.h>

/***********************************************************************/

extern mca_base_framework_t opal_memory_base_framework;

opal_common_ucx_module_t opal_common_ucx = {
    .verbose = 0,
    .progress_iters_mask = 100,
    .registered = 0,
    .opal_mem_hooks = 1,
    .tls = NULL,
    .devices = NULL,
    .ref_count = 0,
    .first_version = NULL
};

static opal_mutex_t opal_common_ucx_mutex = OPAL_MUTEX_STATIC_INIT;

static void opal_common_ucx_mem_release_cb(void *buf, size_t length, void *cbdata, bool from_alloc)
{
    ucm_vm_munmap(buf, length);
}

ucs_thread_mode_t opal_common_ucx_thread_mode(int ompi_mode)
{
    switch (ompi_mode) {
    case MPI_THREAD_MULTIPLE:
        return UCS_THREAD_MODE_MULTI;
    case MPI_THREAD_SERIALIZED:
        return UCS_THREAD_MODE_SERIALIZED;
    case MPI_THREAD_FUNNELED:
    case MPI_THREAD_SINGLE:
        return UCS_THREAD_MODE_SINGLE;
    default:
        MCA_COMMON_UCX_WARN("Unknown MPI thread mode %d, using multithread",
                            ompi_mode);
        return UCS_THREAD_MODE_MULTI;
    }
}

static void opal_common_ucx_convert_count_to_mask(unsigned *progress_iters_mask)
{
    *progress_iters_mask = opal_next_poweroftwo(*progress_iters_mask) - 1;
}

OPAL_DECLSPEC void opal_common_ucx_mca_var_register(const mca_base_component_t *component)
{
    char *default_tls = "rc_verbs,ud_verbs,rc_mlx5,dc_mlx5,ud_mlx5,cuda_ipc,rocm_ipc";
    char *default_devices = "mlx*,hns*";
    int hook_index;
    int verbose_index;
    int progress_index;
    int tls_index;
    int devices_index;

#if HAVE_DECL_UCP_WORKER_FLAG_IGNORE_REQUEST_LEAK
    int request_leak_check;
#endif
#if HAVE_DECL_UCP_OP_ATTR_FLAG_MULTI_SEND
    int multi_send_nb;
    int multi_send_op_attr_enable;
#endif

    OPAL_THREAD_LOCK(&opal_common_ucx_mutex);

    /* It is harmless to re-register variables so go ahead an re-register. */
    verbose_index = mca_base_var_register("opal", "opal_common", "ucx", "verbose",
                                          "Verbose level of the UCX components",
                                          MCA_BASE_VAR_TYPE_INT, NULL, 0,
                                          MCA_BASE_VAR_FLAG_SETTABLE, OPAL_INFO_LVL_3,
                                          MCA_BASE_VAR_SCOPE_LOCAL, &opal_common_ucx.verbose);
    progress_index = mca_base_var_register("opal", "opal_common", "ucx", "progress_iterations",
                                           "Set number of calls of internal UCX progress "
                                           "calls per opal_progress call",
                                           MCA_BASE_VAR_TYPE_INT, NULL, 0,
                                           MCA_BASE_VAR_FLAG_SETTABLE, OPAL_INFO_LVL_3,
                                           MCA_BASE_VAR_SCOPE_LOCAL,
                                           &opal_common_ucx.progress_iters_mask);
    opal_common_ucx_convert_count_to_mask(&opal_common_ucx.progress_iters_mask);
    hook_index = mca_base_var_register("opal", "opal_common", "ucx", "opal_mem_hooks",
                                       "Use OPAL memory hooks, instead of UCX internal "
                                       "memory hooks",
                                       MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0, OPAL_INFO_LVL_3,
                                       MCA_BASE_VAR_SCOPE_LOCAL,
                                       &opal_common_ucx.opal_mem_hooks);

    if (NULL == opal_common_ucx.tls) {
        // Extra level of string indirection needed to make ompi_info
        // happy since it will unload this library before the MCA base
        // cleans up the MCA vars. This will cause the string to go
        // out of scope unless we place the pointer to it on the heap.
        opal_common_ucx.tls = (char **) malloc(sizeof(char *));
        *opal_common_ucx.tls = NULL;
    }

    if (NULL == *opal_common_ucx.tls) {
        *opal_common_ucx.tls = strdup(default_tls);
    }

    tls_index = mca_base_var_register(
        "opal", "opal_common", "ucx", "tls",
        "List of UCX transports which should be supported on the system, to enable "
        "selecting the UCX component. Special values: any (any available). "
        "A '^' prefix negates the list. "
        "For example, in order to exclude on shared memory and TCP transports, "
        "please set to '^posix,sysv,self,tcp,cma,knem,xpmem'.",
        MCA_BASE_VAR_TYPE_STRING, NULL, 0, MCA_BASE_VAR_FLAG_SETTABLE | MCA_BASE_VAR_FLAG_DWG,
        OPAL_INFO_LVL_3, MCA_BASE_VAR_SCOPE_LOCAL, opal_common_ucx.tls);

    if (NULL == opal_common_ucx.devices) {
        opal_common_ucx.devices = (char**) malloc(sizeof(char*));
        *opal_common_ucx.devices = NULL;
    }

    if (NULL == *opal_common_ucx.devices) {
        *opal_common_ucx.devices = strdup(default_devices);
    }

    devices_index = mca_base_var_register(
        "opal", "opal_common", "ucx", "devices",
        "List of device driver pattern names, which, if supported by UCX, will "
        "bump its priority above ob1. Special values: any (any available)",
        MCA_BASE_VAR_TYPE_STRING, NULL, 0, MCA_BASE_VAR_FLAG_SETTABLE | MCA_BASE_VAR_FLAG_DWG,
        OPAL_INFO_LVL_3, MCA_BASE_VAR_SCOPE_LOCAL, opal_common_ucx.devices);

#if HAVE_DECL_UCP_WORKER_FLAG_IGNORE_REQUEST_LEAK
    opal_common_ucx.request_leak_check = false;
    request_leak_check = mca_base_var_register(
        "opal", "opal_common", "ucx", "request_leak_check",
        "Enable showing a warning during MPI_Finalize if some "
        "non-blocking MPI requests have not been released",
        MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0, OPAL_INFO_LVL_3, MCA_BASE_VAR_SCOPE_LOCAL,
        &opal_common_ucx.request_leak_check);
#else
    /* If UCX does not support ignoring leak check, then it's always enabled */
    opal_common_ucx.request_leak_check = true;
#endif

    opal_common_ucx.op_attr_nonblocking = 0;
#if HAVE_DECL_UCP_OP_ATTR_FLAG_MULTI_SEND
    multi_send_op_attr_enable = 1;
    multi_send_nb = mca_base_var_register(
        "opal", "opal_common", "ucx", "multi_send_nb",
        "Enable passing multi-send optimization flag for nonblocking operations",
        MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0, OPAL_INFO_LVL_3, MCA_BASE_VAR_SCOPE_LOCAL,
        &multi_send_op_attr_enable);
#endif

    if (component) {
        mca_base_var_register_synonym(verbose_index, component->mca_project_name,
                                      component->mca_type_name, component->mca_component_name,
                                      "verbose", 0);
        mca_base_var_register_synonym(progress_index, component->mca_project_name,
                                      component->mca_type_name, component->mca_component_name,
                                      "progress_iterations", 0);
        mca_base_var_register_synonym(hook_index, component->mca_project_name,
                                      component->mca_type_name, component->mca_component_name,
                                      "opal_mem_hooks", 0);
        mca_base_var_register_synonym(tls_index, component->mca_project_name,
                                      component->mca_type_name, component->mca_component_name,
                                      "tls", 0);
        mca_base_var_register_synonym(devices_index, component->mca_project_name,
                                      component->mca_type_name, component->mca_component_name,
                                      "devices", 0);
#if HAVE_DECL_UCP_WORKER_FLAG_IGNORE_REQUEST_LEAK
        mca_base_var_register_synonym(request_leak_check, component->mca_project_name,
                                      component->mca_type_name, component->mca_component_name,
                                      "request_leak_check", 0);
#endif
#if HAVE_DECL_UCP_OP_ATTR_FLAG_MULTI_SEND
        mca_base_var_register_synonym(multi_send_nb, component->mca_project_name,
                                      component->mca_type_name, component->mca_component_name,
                                      "multi_send_nb", 0);
#endif
    }

#if HAVE_DECL_UCP_OP_ATTR_FLAG_MULTI_SEND
    if (multi_send_op_attr_enable) {
        opal_common_ucx.op_attr_nonblocking = UCP_OP_ATTR_FLAG_MULTI_SEND;
    }
#endif

    OPAL_THREAD_UNLOCK(&opal_common_ucx_mutex);
}

OPAL_DECLSPEC void opal_common_ucx_mca_register(void)
{
    int ret;

    opal_common_ucx.registered++;
    if (opal_common_ucx.registered > 1) {
        /* process once */
        return;
    }

    opal_common_ucx.output = opal_output_open(NULL);
    opal_output_set_verbosity(opal_common_ucx.output, opal_common_ucx.verbose);

    /* Set memory hooks */
    if (opal_common_ucx.opal_mem_hooks) {
        ret = mca_base_framework_open(&opal_memory_base_framework, 0);
        if (OPAL_SUCCESS != ret) {
            /* failed to initialize memory framework - just exit */
            MCA_COMMON_UCX_VERBOSE(1,
                                   "failed to initialize memory base framework: %d, "
                                   "memory hooks will not be used",
                                   ret);
            return;
        }

        if ((OPAL_MEMORY_FREE_SUPPORT | OPAL_MEMORY_MUNMAP_SUPPORT)
            == ((OPAL_MEMORY_FREE_SUPPORT | OPAL_MEMORY_MUNMAP_SUPPORT)
                & opal_mem_hooks_support_level())) {
            MCA_COMMON_UCX_VERBOSE(1, "%s", "using OPAL memory hooks as external events");
            ucm_set_external_event(UCM_EVENT_VM_UNMAPPED);
            opal_mem_hooks_register_release(opal_common_ucx_mem_release_cb, NULL);
        }
    }
}

OPAL_DECLSPEC void opal_common_ucx_mca_deregister(void)
{
    /* unregister only on last deregister */
    opal_common_ucx.registered--;
    assert(opal_common_ucx.registered >= 0);
    if (opal_common_ucx.registered) {
        return;
    }
    opal_mem_hooks_unregister_release(opal_common_ucx_mem_release_cb);
    opal_output_close(opal_common_ucx.output);
}

#if HAVE_DECL_OPEN_MEMSTREAM
static bool opal_common_ucx_check_device(const char *device_name, char **device_list)
{
    char sysfs_driver_link[OPAL_PATH_MAX];
    char driver_path[OPAL_PATH_MAX];
    char ib_device_name[NAME_MAX];
    char *driver_name;
    char **list_item;
    ssize_t ret;
    char ib_device_name_fmt[NAME_MAX];

    /* mlx5_0:1 */
    opal_snprintf(ib_device_name_fmt, sizeof(ib_device_name_fmt),
                  "%%%u[^:]%%*d", NAME_MAX - 1);
    ret = sscanf(device_name, ib_device_name_fmt, &ib_device_name);
    if (ret != 1) {
        return false;
    }

    sysfs_driver_link[sizeof(sysfs_driver_link) - 1] = '\0';
    snprintf(sysfs_driver_link, sizeof(sysfs_driver_link) - 1,
             "/sys/class/infiniband/%s/device/driver", ib_device_name);

    ret = readlink(sysfs_driver_link, driver_path, sizeof(driver_path) - 1);
    if (ret < 0) {
        MCA_COMMON_UCX_VERBOSE(2, "readlink(%s) failed: %s", sysfs_driver_link, strerror(errno));
        return false;
    }
    driver_path[ret] = '\0'; /* readlink does not append \0 */

    driver_name = basename(driver_path);
    for (list_item = device_list; *list_item != NULL; ++list_item) {
        if (!fnmatch(*list_item, driver_name, 0)) {
            MCA_COMMON_UCX_VERBOSE(2, "driver '%s' matched by '%s'", driver_path, *list_item);
            return true;
        }
    }

    return false;
}
#endif

OPAL_DECLSPEC opal_common_ucx_support_level_t opal_common_ucx_support_level(ucp_context_h context)
{
    opal_common_ucx_support_level_t support_level = OPAL_COMMON_UCX_SUPPORT_NONE;
    static const char *support_level_names[]
        = {[OPAL_COMMON_UCX_SUPPORT_NONE] = "none",
           [OPAL_COMMON_UCX_SUPPORT_TRANSPORT] = "transports only",
           [OPAL_COMMON_UCX_SUPPORT_DEVICE] = "transports and devices"};
#if HAVE_DECL_OPEN_MEMSTREAM
    char rsc_tl_name[NAME_MAX], rsc_device_name[NAME_MAX];
    char rsc_name_fmt[NAME_MAX];
    char **tl_list, **device_list, **list_item;
    bool is_any_tl, is_any_device;
    bool found_tl, negate;
    char line[128];
    FILE *stream;
    char *buffer;
    size_t size;
    int ret;
#endif

    if ((*opal_common_ucx.tls == NULL) || (*opal_common_ucx.devices == NULL)) {
        opal_common_ucx_mca_var_register(NULL);
    }

    is_any_tl = !strcmp(*opal_common_ucx.tls, "any");
    is_any_device = !strcmp(*opal_common_ucx.devices, "any");

    /* Check for special value "any" */
    if (is_any_tl && is_any_device) {
        MCA_COMMON_UCX_VERBOSE(1, "ucx is enabled on any transport or device from %s",
                               *opal_common_ucx.tls);
        support_level = OPAL_COMMON_UCX_SUPPORT_DEVICE;
        goto out;
    }

#if HAVE_DECL_OPEN_MEMSTREAM
    /* Split transports list */
    negate = ('^' == (*opal_common_ucx.tls)[0]);
    tl_list = opal_argv_split(*opal_common_ucx.tls + (negate ? 1 : 0), ',');
    if (tl_list == NULL) {
        MCA_COMMON_UCX_VERBOSE(1, "failed to split tl list '%s', ucx is disabled",
                               *opal_common_ucx.tls);
        goto out;
    }

    /* Split devices list */
    device_list = opal_argv_split(*opal_common_ucx.devices, ',');
    if (device_list == NULL) {
        MCA_COMMON_UCX_VERBOSE(1, "failed to split devices list '%s', ucx is disabled",
                               *opal_common_ucx.devices);
        goto out_free_tl_list;
    }

    /* Open memory stream to dump UCX information to */
    stream = open_memstream(&buffer, &size);
    if (stream == NULL) {
        MCA_COMMON_UCX_VERBOSE(1,
                               "failed to open memory stream for ucx info (%s), "
                               "ucx is disabled",
                               strerror(errno));
        goto out_free_device_list;
    }

    /* Print ucx transports information to the memory stream */
    ucp_context_print_info(context, stream);

    /* "# resource 6  :  md 5  dev 4  flags -- rc_verbs/mlx5_0:1" */
    opal_snprintf(rsc_name_fmt, sizeof(rsc_name_fmt),
        "# resource %%*d : md %%*d dev %%*d flags -- %%%u[^/ \n\r]/%%%u[^/ \n\r]",
        NAME_MAX - 1, NAME_MAX - 1);

    /* Rewind and read transports/devices list from the stream */
    fseek(stream, 0, SEEK_SET);
    while ((support_level != OPAL_COMMON_UCX_SUPPORT_DEVICE)
           && (fgets(line, sizeof(line), stream) != NULL)) {
        ret = sscanf(line, rsc_name_fmt, rsc_tl_name, rsc_device_name);
        if (ret != 2) {
            continue;
        }

        /* Check if 'rsc_tl_name' is found  provided list */
        found_tl = is_any_tl;
        for (list_item = tl_list; !found_tl && (*list_item != NULL); ++list_item) {
            found_tl = !strcmp(*list_item, rsc_tl_name);
        }

        /* Check if the transport has a match (either positive or negative) */
        assert(!(is_any_tl && negate));
        if (found_tl != negate) {
            if (is_any_device || opal_common_ucx_check_device(rsc_device_name, device_list)) {
                MCA_COMMON_UCX_VERBOSE(2, "%s/%s: matched both transport and device list",
                                       rsc_tl_name, rsc_device_name);
                support_level = OPAL_COMMON_UCX_SUPPORT_DEVICE;
            } else {
                MCA_COMMON_UCX_VERBOSE(2, "%s/%s: matched transport list but not device list",
                                       rsc_tl_name, rsc_device_name);
                support_level = OPAL_COMMON_UCX_SUPPORT_TRANSPORT;
            }
        } else {
            MCA_COMMON_UCX_VERBOSE(2, "%s/%s: did not match transport list", rsc_tl_name,
                                   rsc_device_name);
        }
    }

    MCA_COMMON_UCX_VERBOSE(2, "support level is %s", support_level_names[support_level]);
    fclose(stream);
    free(buffer);

out_free_device_list:
    opal_argv_free(device_list);
out_free_tl_list:
    opal_argv_free(tl_list);
out:
#else
    MCA_COMMON_UCX_VERBOSE(2, "open_memstream() was not found, ucx is disabled");
#endif
    return support_level;
}

void opal_common_ucx_empty_complete_cb(void *request, ucs_status_t status)
{
}

static void opal_common_ucx_mca_fence_complete_cb(int status, void *fenced)
{
    *(int *) fenced = 1;
}

#if HAVE_DECL_UCM_TEST_EVENTS
static ucs_status_t opal_common_ucx_mca_test_external_events(int events)
{
#    if HAVE_DECL_UCM_TEST_EXTERNAL_EVENTS
    return ucm_test_external_events(UCM_EVENT_VM_UNMAPPED);
#    else
    return ucm_test_events(UCM_EVENT_VM_UNMAPPED);
#    endif
}

static void opal_common_ucx_mca_test_events(void)
{
    static int warned = 0;
    const char *suggestion;
    ucs_status_t status;

    if (!warned) {
        if (opal_common_ucx.opal_mem_hooks) {
            suggestion = "Please check OPAL memory events infrastructure.";
            status = opal_common_ucx_mca_test_external_events(UCM_EVENT_VM_UNMAPPED);
        } else {
            suggestion = "Pls try adding --mca opal_common_ucx_opal_mem_hooks 1 "
                         "to mpirun/oshrun command line to resolve this issue.";
            status = ucm_test_events(UCM_EVENT_VM_UNMAPPED);
        }

        if (status != UCS_OK) {
            MCA_COMMON_UCX_WARN("UCX is unable to handle VM_UNMAP event. "
                                "This may cause performance degradation or data "
                                "corruption. %s",
                                suggestion);
            warned = 1;
        }
    }
}
#endif

void opal_common_ucx_mca_proc_added(void)
{
#if HAVE_DECL_UCM_TEST_EVENTS
    opal_common_ucx_mca_test_events();
#endif
}

OPAL_DECLSPEC int opal_common_ucx_mca_pmix_fence_nb(int *fenced)
{
    return PMIx_Fence_nb(NULL, 0, NULL, 0, opal_common_ucx_mca_fence_complete_cb, (void *) fenced);
}

OPAL_DECLSPEC int opal_common_ucx_mca_pmix_fence(ucp_worker_h worker)
{
    volatile int fenced = 0;
    int ret = OPAL_SUCCESS;

    if (OPAL_SUCCESS
        != (ret = PMIx_Fence_nb(NULL, 0, NULL, 0, opal_common_ucx_mca_fence_complete_cb,
                                (void *) &fenced))) {
        return ret;
    }

    MCA_COMMON_UCX_PROGRESS_LOOP(worker) {
        if(fenced) {
            break;
        }
    }

    return ret;
}

static void opal_common_ucx_wait_all_requests(void **reqs, int count,
        ucp_worker_h worker, enum opal_common_ucx_req_type type)
{
    int i;

    MCA_COMMON_UCX_VERBOSE(2, "waiting for %d disconnect requests", count);
    for (i = 0; i < count; ++i) {
        opal_common_ucx_wait_request(reqs[i], worker, type, "ucp_disconnect_nb");
        reqs[i] = NULL;
    }
}

OPAL_DECLSPEC int opal_common_ucx_del_procs_nofence(opal_common_ucx_del_proc_t *procs, size_t count,
                                                    size_t my_rank, size_t max_disconnect,
                                                    ucp_worker_h worker)
{
    size_t num_reqs;
    size_t max_reqs;
    void *dreq, **dreqs;
    size_t i;
    size_t n;

    MCA_COMMON_UCX_ASSERT(procs || !count);
    MCA_COMMON_UCX_ASSERT(max_disconnect > 0);

    max_reqs = (max_disconnect > count) ? count : max_disconnect;

    dreqs = malloc(sizeof(*dreqs) * max_reqs);
    if (dreqs == NULL) {
        return OPAL_ERR_OUT_OF_RESOURCE;
    }

    num_reqs = 0;

    for (i = 0; i < count; ++i) {
        n = (i + my_rank) % count;
        if (procs[n].ep == NULL) {
            continue;
        }

        MCA_COMMON_UCX_VERBOSE(2, "disconnecting from rank %zu", procs[n].vpid);
        dreq = ucp_disconnect_nb(procs[n].ep);
        if (dreq != NULL) {
            if (UCS_PTR_IS_ERR(dreq)) {
                MCA_COMMON_UCX_ERROR("ucp_disconnect_nb(%zu) failed: %s", procs[n].vpid,
                                     ucs_status_string(UCS_PTR_STATUS(dreq)));
                continue;
            } else {
                dreqs[num_reqs++] = dreq;
                if (num_reqs >= max_disconnect) {
                    opal_common_ucx_wait_all_requests(dreqs, num_reqs, worker,
                            OPAL_COMMON_UCX_REQUEST_TYPE_UCP);
                    num_reqs = 0;
                }
            }
        }
    }
    /* num_reqs == 0 is processed by opal_common_ucx_wait_all_requests routine,
     * so suppress coverity warning */
    /* coverity[uninit_use_in_call] */
    opal_common_ucx_wait_all_requests(dreqs, num_reqs, worker,
            OPAL_COMMON_UCX_REQUEST_TYPE_UCP);
    free(dreqs);

    return OPAL_SUCCESS;
}

OPAL_DECLSPEC int opal_common_ucx_del_procs(opal_common_ucx_del_proc_t *procs, size_t count,
                                            size_t my_rank, size_t max_disconnect,
                                            ucp_worker_h worker)
{
    opal_common_ucx_del_procs_nofence(procs, count, my_rank, max_disconnect, worker);

    return opal_common_ucx_mca_pmix_fence(worker);
}

static void safety_valve(void) __opal_attribute_destructor__;
void safety_valve(void) {
    opal_mem_hooks_unregister_release(opal_common_ucx_mem_release_cb);
}

#if HAVE_UCP_WORKER_ADDRESS_FLAGS
static int opal_common_ucx_send_worker_address_type(const mca_base_component_t *version,
                                                    int addr_flags, int modex_scope)
{
    ucs_status_t status;
    ucp_worker_attr_t attrs;
    int rc;

    attrs.field_mask    = UCP_WORKER_ATTR_FIELD_ADDRESS |
                          UCP_WORKER_ATTR_FIELD_ADDRESS_FLAGS;
    attrs.address_flags = addr_flags;

    status = ucp_worker_query(opal_common_ucx.ucp_worker, &attrs);
    if (UCS_OK != status) {
        MCA_COMMON_UCX_ERROR("Failed to query UCP worker address");
        return OPAL_ERROR;
    }

    OPAL_MODEX_SEND(rc, modex_scope, version, (void*)attrs.address, attrs.address_length);

    ucp_worker_release_address(opal_common_ucx.ucp_worker, attrs.address);

    if (OPAL_SUCCESS != rc) {
        return OPAL_ERROR;
    }

    MCA_COMMON_UCX_VERBOSE(2, "Pack %s worker address, size %ld",
                    (modex_scope == PMIX_LOCAL) ? "local" : "remote",
                    attrs.address_length);

    return OPAL_SUCCESS;
}
#endif

static int opal_common_ucx_send_worker_address(const mca_base_component_t *version)
{
    ucs_status_t status;

#if !HAVE_UCP_WORKER_ADDRESS_FLAGS
    ucp_address_t *address;
    size_t addrlen;
    int rc;

    status = ucp_worker_get_address(opal_common_ucx.ucp_worker, &address, &addrlen);
    if (UCS_OK != status) {
        MCA_COMMON_UCX_ERROR("Failed to get worker address");
        return OPAL_ERROR;
    }

    MCA_COMMON_UCX_VERBOSE(2, "Pack worker address, size %ld", addrlen);

    OPAL_MODEX_SEND(rc, PMIX_GLOBAL, version, (void*)address, addrlen);

    ucp_worker_release_address(opal_common_ucx.ucp_worker, address);

    if (OPAL_SUCCESS != rc) {
        goto err;
    }
#else
    /* Pack just network device addresses for remote node peers */
    status = opal_common_ucx_send_worker_address_type(version,
                                                      UCP_WORKER_ADDRESS_FLAG_NET_ONLY,
                                                      PMIX_REMOTE);
    if (UCS_OK != status) {
        goto err;
    }

    status = opal_common_ucx_send_worker_address_type(version, 0, PMIX_LOCAL);
    if (UCS_OK != status) {
        goto err;
    }
#endif

    return OPAL_SUCCESS;

err:
    MCA_COMMON_UCX_ERROR("Open MPI couldn't distribute EP connection details");
    return OPAL_ERROR;
}

int opal_common_ucx_recv_worker_address(opal_process_name_t *proc_name,
                                        ucp_address_t **address_p,
                                        size_t *addrlen_p)
{
    int ret;

    const mca_base_component_t *version = opal_common_ucx.first_version;

    *address_p = NULL;
    OPAL_MODEX_RECV(ret, version, proc_name, (void**)address_p, addrlen_p);
    if (ret < 0) {
        MCA_COMMON_UCX_ERROR("Failed to receive UCX worker address: %s (%d)",
                      opal_strerror(ret), ret);
    }

    return ret;
}

int opal_common_ucx_open(const char *prefix,
#ifdef HAVE_UCG
                         const ucg_params_t *ucg_params,
#else
                         const ucp_params_t *ucp_params,
#endif
                         size_t *request_size)
{
    unsigned major_version, minor_version, release_number;
    ucp_context_attr_t attr;
    ucs_status_t status;
    int just_query = 0;

    if (opal_common_ucx.ref_count++ > 0) {
        just_query = 1;
        goto query;
    }

    /* Check version */
    ucp_get_version(&major_version, &minor_version, &release_number);
    MCA_COMMON_UCX_VERBOSE(1, "opal_common_ucx_open: UCX version %u.%u.%u",
                           major_version, minor_version, release_number);

    if ((major_version == 1) && (minor_version == 8)) {
        /* disabled due to issue #8321 */
        MCA_COMMON_UCX_VERBOSE(1, "UCX is disabled because the run-time UCX"
                               " version is 1.8, which has a known catastrophic"
                               " issue");
        goto open_error;
    }

    if ((major_version == 1) && (minor_version < 9)) {
        /* show warning due to issue #8549 */
        MCA_COMMON_UCX_WARN("UCX version %u.%u.%u is too old, please install "
                            "1.9.x or newer", major_version, minor_version,
                            release_number);
    }

#ifdef HAVE_UCG
    ucg_config_t *config;
    status = ucg_config_read(prefix, NULL, &config);
    if (UCS_OK != status) {
        goto open_error;
    }

    status = ucg_init(ucg_params, config, &opal_common_ucx.ucg_context);
    if (UCS_OK == status) {
        opal_common_ucx.ucb_context =
                ucg_context_get_ucb(opal_common_ucx.ucg_context);
        opal_common_ucx.ucp_context =
                ucb_context_get_ucp(opal_common_ucx.ucb_context);
    }
    ucg_config_release(config);
#else
    ucp_config_t *config;
    status = ucp_config_read(prefix, NULL, &config);
    if (UCS_OK != status) {
        goto open_error;
    }

    status = ucp_init(ucp_params, config, &opal_common_ucx.ucp_context);
    ucp_config_release(config);
#endif

    if (UCS_OK != status) {
        goto open_error;
    }

query:
    /* Consider the case where creation failed */
    if (!opal_common_ucx.ucp_context) {
        goto open_error;
    }

    /* Query UCX attributes */
    attr.field_mask  = UCP_ATTR_FIELD_REQUEST_SIZE;
#if HAVE_UCP_ATTR_MEMORY_TYPES
    attr.field_mask |= UCP_ATTR_FIELD_MEMORY_TYPES;
#endif
    status = ucp_context_query(opal_common_ucx.ucp_context, &attr);
    if (UCS_OK != status) {
        goto cleanup_ctx;
    }

    *request_size = attr.request_size;
    if (just_query) {
        return OPAL_SUCCESS;
    }

    return OPAL_SUCCESS;

cleanup_ctx:
#ifdef HAVE_UCG
    ucg_cleanup(opal_common_ucx.ucg_context);
    opal_common_ucx.ucg_context = NULL;
#else
    ucp_cleanup(opal_common_ucx.ucp_context);
#endif

open_error:
    opal_common_ucx.ucp_context = NULL; /* In case anyone comes querying */
    return OPAL_ERROR;
}

int opal_common_ucx_close(void)
{
    MCA_COMMON_UCX_VERBOSE(1, "opal_common_ucx_close");

    if (opal_common_ucx.ref_count > 0) {
        return OPAL_SUCCESS;
    }

    if (opal_common_ucx.ucp_context != NULL) {
        ucp_cleanup(opal_common_ucx.ucp_context);
        opal_common_ucx.ucp_context = NULL;
    }

    return OPAL_SUCCESS;
}

static int opal_common_ucx_init_worker(ucs_thread_mode_t thread_mode)
{
    ucp_worker_params_t params;
    ucp_worker_attr_t attr;
    ucs_status_t status;
    int rc;

    params.field_mask  = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    params.thread_mode = thread_mode;

    status = ucp_worker_create(opal_common_ucx.ucp_context, &params,
                               &opal_common_ucx.ucp_worker);
    if (UCS_OK != status) {
        MCA_COMMON_UCX_ERROR("Failed to create UCP worker");
        return OPAL_ERROR;
    }

#if HAVE_DECL_UCP_WORKER_FLAG_IGNORE_REQUEST_LEAK
    if (!opal_common_ucx.request_leak_check) {
        params.field_mask |= UCP_WORKER_PARAM_FIELD_FLAGS;
        params.flags      |= UCP_WORKER_FLAG_IGNORE_REQUEST_LEAK;
    }
#endif

#if HAVE_DECL_UCP_WORKER_PARAM_FIELD_UUID
    params.field_mask |= UCP_WORKER_PARAM_FIELD_UUID;
    params.uuid       |= opal_process_info.myprocid.rank;
#endif

    attr.field_mask = UCP_WORKER_ATTR_FIELD_THREAD_MODE;
    status = ucp_worker_query(opal_common_ucx.ucp_worker, &attr);
    if (UCS_OK != status) {
        MCA_COMMON_UCX_ERROR("Failed to query UCP worker thread level");
        rc = OPAL_ERROR;
        goto err_destroy_worker;
    }

    if ((thread_mode == UCS_THREAD_MODE_MULTI) &&
        (attr.thread_mode != UCS_THREAD_MODE_MULTI)) {
        /* UCX does not support multithreading, disqualify component for now */
        /* TODO: we should let OMPI to fallback to THREAD_SINGLE mode */
        MCA_COMMON_UCX_WARN("UCP worker does not support MPI_THREAD_MULTIPLE");
        rc = OPAL_ERR_NOT_SUPPORTED;
        goto err_destroy_worker;
    }

    MCA_COMMON_UCX_VERBOSE(2, "created ucp context %p, worker %p",
                           (void *)opal_common_ucx.ucp_context,
                           (void *)opal_common_ucx.ucp_worker);

    return OPAL_SUCCESS;

err_destroy_worker:
    ucp_worker_destroy(opal_common_ucx.ucp_worker);
    return rc;
}

static int opal_common_ucx_progress(void)
{
    return (int) ucp_worker_progress(opal_common_ucx.ucp_worker);
}

int opal_common_ucx_init(int enable_mpi_threads,
                         const mca_base_component_t *version)
{
    int rc;

    if (opal_common_ucx.first_version != NULL) {
        return OPAL_SUCCESS;
    }

    rc = opal_common_ucx_init_worker(enable_mpi_threads);
    if (rc < 0) {
        return rc;
    }

    rc = opal_common_ucx_send_worker_address(version);
    if (rc < 0) {
        MCA_COMMON_UCX_ERROR("Failed to send worker address")
        ucp_worker_destroy(opal_common_ucx.ucp_worker);
    } else {
        opal_common_ucx.first_version = version;
    }

    opal_progress_register(opal_common_ucx_progress);

    return rc;
}

int opal_common_ucx_cleanup(void)
{
    MCA_COMMON_UCX_ASSERT(opal_common_ucx.ref_count > 0);

    if (--opal_common_ucx.ref_count > 1) {
        return OPAL_SUCCESS;
    }

    opal_progress_unregister(opal_common_ucx_progress);

    if (opal_common_ucx.ucp_worker != NULL) {
        ucp_worker_destroy(opal_common_ucx.ucp_worker);
        opal_common_ucx.ucp_worker = NULL;
    }

    return OPAL_SUCCESS;
}
