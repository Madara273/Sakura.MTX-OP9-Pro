/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_KGSL_GPU_MEM_H
#define _LINUX_KGSL_GPU_MEM_H

#include <linux/errno.h>
#include <linux/kconfig.h>
#include <linux/notifier.h>
#include <linux/types.h>

struct kgsl_gpu_mem_event {
	u32 pid;
	u64 size;
	u64 total;
};

#if defined(CONFIG_BPF_ANDROID_GPU_MEM_FALLBACK) && \
	IS_BUILTIN(CONFIG_QCOM_KGSL) && !defined(CONFIG_BPF_EVENTS)
int kgsl_gpu_mem_register_notifier(struct notifier_block *nb);
void kgsl_gpu_mem_unregister_notifier(struct notifier_block *nb);
#else
static inline int kgsl_gpu_mem_register_notifier(struct notifier_block *nb)
{
	return -EOPNOTSUPP;
}

static inline void kgsl_gpu_mem_unregister_notifier(struct notifier_block *nb)
{
}
#endif

#endif /* _LINUX_KGSL_GPU_MEM_H */
