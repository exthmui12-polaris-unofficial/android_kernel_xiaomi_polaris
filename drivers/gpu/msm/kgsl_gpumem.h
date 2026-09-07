/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KGSL_GPUMEM_H
#define __KGSL_GPUMEM_H

struct kgsl_memdesc;
struct kgsl_mem_entry;
struct kgsl_process_private;

void kgsl_gpumem_alloc(struct kgsl_memdesc *memdesc, const void *key);
void kgsl_gpumem_free(struct kgsl_memdesc *memdesc);
void kgsl_gpumem_commit(struct kgsl_mem_entry *entry, const void *key);
void kgsl_gpumem_uncommit(struct kgsl_mem_entry *entry);
void kgsl_gpumem_process_add(struct kgsl_process_private *process);
void kgsl_gpumem_process_remove(struct kgsl_process_private *process);

#endif
