/* SPDX-License-Identifier: GPL-2.0 */
/* Controlled KGSL allocations for comparing sysfs, tracepoint and BPF totals. */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "../../../../include/uapi/linux/msm_kgsl.h"
#include "../../../../include/uapi/linux/ion.h"

/* Polaris include/uapi/linux/msm_ion.h: ION_SYSTEM_HEAP_ID = 25. */
#define POLARIS_ION_SYSTEM_HEAP_ID 25

static int gpu;
static unsigned ids[64];
static int sparse_ids[64];
static int ion_fds[16];

struct allocation {
    uint64_t size;
    unsigned id;
    int error;
    pid_t tid;
};

static void *allocate_thread(void *arg)
{
    struct allocation *result = arg;
    struct kgsl_gpuobj_alloc request = { .size = result->size };
    result->tid = gettid();
    if (ioctl(gpu, IOCTL_KGSL_GPUOBJ_ALLOC, &request) < 0)
        result->error = errno;
    else {
        result->size = request.size;
        result->id = request.id;
    }
    return NULL;
}

static void reply(int error, int slot, unsigned id, uint64_t size, pid_t tid)
{
    printf("{\"error\":%d,\"pid\":%d,\"tid\":%d,\"slot\":%d,\"id\":%u,\"size\":%" PRIu64 "}\n",
           error, getpid(), tid, slot, id, size);
}

int main(void)
{
    char line[128], command[32];
    uint64_t value;
    setbuf(stdout, NULL);
    for (unsigned i = 0; i < 16; i++) ion_fds[i] = -1;
    gpu = open("/dev/kgsl-3d0", O_RDWR | O_CLOEXEC);
    if (gpu < 0) { perror("open kgsl"); return 1; }
    reply(0, -1, 0, 0, gettid());
    while (fgets(line, sizeof(line), stdin)) {
        value = 0;
        if (sscanf(line, "%31s %" SCNu64, command, &value) < 1) continue;
        if (!strcmp(command, "quit")) break;
        if (!strcmp(command, "exit")) _exit(0); /* Let KGSL release live objects. */
        int error = 0, slot = -1;
        unsigned id = 0;
        uint64_t size = 0;
        pid_t tid = gettid();
        if (!strcmp(command, "free")) {
            if (value >= 64 || !ids[value]) error = EINVAL;
            else {
                int ret;
                if (sparse_ids[value]) {
                    struct kgsl_sparse_virt_free request = { .id = ids[value] };
                    ret = ioctl(gpu, IOCTL_KGSL_SPARSE_VIRT_FREE, &request);
                } else {
                    struct kgsl_gpumem_free_id request = { .id = ids[value] };
                    ret = ioctl(gpu, IOCTL_KGSL_GPUMEM_FREE_ID, &request);
                }
                if (ret < 0) error = errno;
                else { ids[value] = 0; sparse_ids[value] = 0; }
            }
        } else if (!strcmp(command, "invalid")) {
            struct kgsl_gpuobj_alloc request = { .size = 0 };
            if (ioctl(gpu, IOCTL_KGSL_GPUOBJ_ALLOC, &request) >= 0) error = EPROTO;
            else if (errno != EINVAL) error = errno;
        } else if (!strcmp(command, "ion")) {
            for (unsigned i = 0; i < 16; i++)
                if (ion_fds[i] < 0) { slot = i; break; }
            if (slot < 0 || !value || value > 64 * 1024 * 1024) error = EINVAL;
            else {
                int ion = open("/dev/ion", O_RDWR | O_CLOEXEC);
                if (ion < 0) error = errno;
                else {
                    struct ion_allocation_data alloc = {
                        .len = value, .align = 4096,
                        .heap_id_mask = 1U << POLARIS_ION_SYSTEM_HEAP_ID,
                    };
                    if (ioctl(ion, ION_IOC_ALLOC, &alloc) < 0) error = errno;
                    else {
                        struct ion_fd_data share = { .handle = alloc.handle };
                        struct ion_handle_data release = { .handle = alloc.handle };
                        if (ioctl(ion, ION_IOC_SHARE, &share) < 0) error = errno;
                        else { ion_fds[slot] = share.fd; size = value; }
                        if (ioctl(ion, ION_IOC_FREE, &release) < 0 && !error) error = errno;
                    }
                    close(ion);
                }
            }
        } else {
            for (unsigned i = 0; i < 64; i++)
                if (!ids[i]) { slot = i; break; }
            if (slot < 0) error = ENOSPC;
            else if (!strcmp(command, "import")) {
                if (value >= 16 || ion_fds[value] < 0) error = EINVAL;
                else {
                    struct kgsl_gpuobj_import_dma_buf data = { .fd = ion_fds[value] };
                    struct kgsl_gpuobj_import request = {
                        .priv = (uintptr_t)&data, .priv_len = sizeof(data),
                        .type = KGSL_USER_MEM_TYPE_DMABUF,
                    };
                    if (ioctl(gpu, IOCTL_KGSL_GPUOBJ_IMPORT, &request) < 0) error = errno;
                    else id = request.id;
                }
            } else if (!value || value > 64 * 1024 * 1024) error = EINVAL;
            else if (!strcmp(command, "alloc") || !strcmp(command, "thread")) {
                struct allocation result = { .size = value };
                if (!strcmp(command, "thread")) {
                    pthread_t worker;
                    error = pthread_create(&worker, NULL, allocate_thread, &result);
                    if (!error) error = pthread_join(worker, NULL);
                } else allocate_thread(&result);
                if (!error) error = result.error;
                id = result.id; size = result.size; tid = result.tid;
            } else if (!strcmp(command, "legacy")) {
                struct kgsl_gpumem_alloc_id request = { .size = value };
                if (ioctl(gpu, IOCTL_KGSL_GPUMEM_ALLOC_ID, &request) < 0) error = errno;
                else { id = request.id; size = request.size; }
            } else if (!strcmp(command, "sparse")) {
                struct kgsl_sparse_virt_alloc request = { .size = value, .pagesize = 4096 };
                if (ioctl(gpu, IOCTL_KGSL_SPARSE_VIRT_ALLOC, &request) < 0) error = errno;
                else { id = request.id; size = request.size; sparse_ids[slot] = 1; }
            } else error = EINVAL;
            if (!error) ids[slot] = id;
        }
        reply(error, slot, id, size, tid);
    }
    close(gpu);
    for (unsigned i = 0; i < 16; i++) if (ion_fds[i] >= 0) close(ion_fds[i]);
    return 0;
}
