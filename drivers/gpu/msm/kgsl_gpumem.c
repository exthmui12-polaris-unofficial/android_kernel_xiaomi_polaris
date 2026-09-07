/* SPDX-License-Identifier: GPL-2.0 */
/* Android 12 GPU accounting for the Polaris KGSL driver. */
#include <linux/hashtable.h>

#include "kgsl.h"
#include "kgsl_device.h"
#include "kgsl_gpumem.h"

#define CREATE_TRACE_POINTS
#include <trace/events/gpu_mem.h>

static DEFINE_SPINLOCK(gpu_mem_lock);
static DEFINE_HASHTABLE(gpu_mem_buffers, 8);
static DEFINE_HASHTABLE(gpu_mem_processes, 6);
static u64 gpu_mem_total;

/* Counters live from boot: events report totals, including pre-attach memory. */

/* A PID can close/reopen KGSL while an older file's deferred frees remain. */
static u64 process_total(u32 pid)
{
	struct kgsl_process_private *process;
	u64 total = 0;

	hash_for_each_possible(gpu_mem_processes, process, gpu_mem_node, pid)
		if (pid_nr(process->pid) == pid)
			total += process->gpu_mem_total;
	return total;
}

/*
 * Like Qualcomm's Android 12 accounting, count imported DMA buffers only
 * once globally. Each import still contributes to its process's mappings.
 * The existing dma_buf reference keeps the identity alive until unimport.
 */
static void account_buffer(struct kgsl_memdesc *memdesc, const void *key)
{
	struct kgsl_memdesc *other;
	bool shared = false;

	if (memdesc->gpu_mem_size || !memdesc->size ||
			(memdesc->flags & KGSL_MEMFLAGS_SPARSE_VIRT))
		return;

	hash_for_each_possible(gpu_mem_buffers, other, gpu_mem_node,
			(unsigned long)key) {
		if (other->gpu_mem_key == key) {
			shared = true;
			break;
		}
	}
	memdesc->gpu_mem_key = key;
	memdesc->gpu_mem_size = memdesc->size;
	hash_add(gpu_mem_buffers, &memdesc->gpu_mem_node, (unsigned long)key);
	if (!shared) {
		gpu_mem_total += memdesc->gpu_mem_size;
		trace_gpu_mem_total(0, 0, gpu_mem_total);
	}
}

void kgsl_gpumem_alloc(struct kgsl_memdesc *memdesc, const void *key)
{
	unsigned long flags;

	spin_lock_irqsave(&gpu_mem_lock, flags);
	account_buffer(memdesc, key);
	spin_unlock_irqrestore(&gpu_mem_lock, flags);
}

void kgsl_gpumem_free(struct kgsl_memdesc *memdesc)
{
	struct kgsl_memdesc *other;
	unsigned long flags;
	bool shared = false;

	spin_lock_irqsave(&gpu_mem_lock, flags);
	/* Failed allocations/imports that never committed were not counted. */
	if (!memdesc->gpu_mem_size)
		goto out;
	hash_del(&memdesc->gpu_mem_node);
	hash_for_each_possible(gpu_mem_buffers, other, gpu_mem_node,
			(unsigned long)memdesc->gpu_mem_key) {
		if (other->gpu_mem_key == memdesc->gpu_mem_key) {
			shared = true;
			break;
		}
	}
	if (!shared) {
		gpu_mem_total -= memdesc->gpu_mem_size;
		trace_gpu_mem_total(0, 0, gpu_mem_total);
	}
	memdesc->gpu_mem_size = 0;
out:
	spin_unlock_irqrestore(&gpu_mem_lock, flags);
}

/* Called under the entry's process mem_lock, before publishing its ID. */
void kgsl_gpumem_commit(struct kgsl_mem_entry *entry, const void *key)
{
	struct kgsl_process_private *process = entry->priv;
	unsigned long flags;
	u32 pid = pid_nr(process->pid);

	spin_lock_irqsave(&gpu_mem_lock, flags);
	account_buffer(&entry->memdesc, key);
	if (entry->memdesc.gpu_mem_size && !entry->gpu_mem_accounted) {
		process->gpu_mem_total += entry->memdesc.gpu_mem_size;
		entry->gpu_mem_accounted = true;
		trace_gpu_mem_total(0, pid, process_total(pid));
	}
	spin_unlock_irqrestore(&gpu_mem_lock, flags);
}

void kgsl_gpumem_uncommit(struct kgsl_mem_entry *entry)
{
	struct kgsl_process_private *process = entry->priv;
	unsigned long flags;
	u32 pid = pid_nr(process->pid);

	spin_lock_irqsave(&gpu_mem_lock, flags);
	if (entry->gpu_mem_accounted) {
		process->gpu_mem_total -= entry->memdesc.gpu_mem_size;
		entry->gpu_mem_accounted = false;
		/* Zero causes Android's BPF program to delete this PID's entry. */
		trace_gpu_mem_total(0, pid, process_total(pid));
	}
	spin_unlock_irqrestore(&gpu_mem_lock, flags);
}

void kgsl_gpumem_process_add(struct kgsl_process_private *process)
{
	unsigned long flags;

	spin_lock_irqsave(&gpu_mem_lock, flags);
	hash_add(gpu_mem_processes, &process->gpu_mem_node, pid_nr(process->pid));
	spin_unlock_irqrestore(&gpu_mem_lock, flags);
}

void kgsl_gpumem_process_remove(struct kgsl_process_private *process)
{
	unsigned long flags;
	u32 pid = pid_nr(process->pid);

	spin_lock_irqsave(&gpu_mem_lock, flags);
	hash_del(&process->gpu_mem_node);
	trace_gpu_mem_total(0, pid, process_total(pid));
	spin_unlock_irqrestore(&gpu_mem_lock, flags);
}
