/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Google, Inc. */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM gpu_mem

#if !defined(_TRACE_GPU_MEM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GPU_MEM_H

#include <linux/tracepoint.h>

/* Android 12 ABI: gpu_id at 8, pid at 12, total bytes at 16; pid 0 is global. */
TRACE_EVENT(gpu_mem_total,

	TP_PROTO(u32 gpu_id, u32 pid, u64 size),
	TP_ARGS(gpu_id, pid, size),
	TP_STRUCT__entry(
		__field(u32, gpu_id)
		__field(u32, pid)
		__field(u64, size)
	),
	TP_fast_assign(
		__entry->gpu_id = gpu_id;
		__entry->pid = pid;
		__entry->size = size;
	),
	TP_printk("gpu_id=%u pid=%u size=%llu", __entry->gpu_id,
		__entry->pid, __entry->size)
);

#endif

#include <trace/define_trace.h>
