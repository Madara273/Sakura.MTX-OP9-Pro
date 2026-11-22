// SPDX-License-Identifier: GPL-2.0-only
/*
 * Built-in ARM64 transport for KSU-Next's syscall event handlers.
 * Keep an original table for dev callers, which invoke the unhooked syscall
 * through ksu_syscall_table. The live kernel table is patched separately.
 */
#include <linux/errno.h>
#include <linux/compat.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <asm/syscall.h>
#include <asm/unistd.h>

#include "hook/syscall_hook.h"
#include "hook/patch_memory.h"
#include "klog.h"

extern const syscall_fn_t sys_call_table[];

syscall_fn_t *ksu_syscall_table;
int ksu_dispatcher_nr = -1;
static syscall_fn_t *live_syscall_table;
static ksu_syscall_hook_fn syscall_hooks[__NR_syscalls];
static bool patched_syscalls[__NR_syscalls];
static DEFINE_MUTEX(syscall_table_lock);

#ifdef CONFIG_COMPAT
/* ARM EABI syscall numbers from arch/arm64/include/asm/unistd32.h. */
static int compat_syscall_nr(int nr)
{
	switch (nr) {
	case __NR_execve:
		return 11;
	case __NR_execveat:
		return 387;
	case __NR_newfstatat:
		return 327;
	case __NR_faccessat:
		return 334;
	case __NR_reboot:
		return 88;
	default:
		return -1;
	}
}

static syscall_fn_t compat_original[__NR_syscalls];
#endif

long __nocfi ksu_call_original_syscall(int nr, const struct pt_regs *regs)
{
#ifdef CONFIG_COMPAT
	if (is_compat_task()) {
		if (nr < 0 || nr >= __NR_syscalls || !compat_original[nr])
			return -ENOSYS;
		return compat_original[nr](regs);
	}
#endif
	return ksu_syscall_table[nr](regs);
}

static int patch_syscall(int nr, syscall_fn_t fn)
{
	int ret;
#ifdef CONFIG_COMPAT
	syscall_fn_t previous = READ_ONCE(live_syscall_table[nr]);
#endif

	ret = ksu_patch_text(&live_syscall_table[nr], &fn, sizeof(fn),
			     KSU_PATCH_TEXT_FLUSH_DCACHE);
#ifdef CONFIG_COMPAT
	if (!ret && compat_syscall_nr(nr) >= 0) {
		syscall_fn_t compat_fn = fn == ksu_syscall_table[nr] ?
			compat_original[nr] : fn;
		int restore_ret;

		ret = ksu_patch_text((void *)&compat_sys_call_table[compat_syscall_nr(nr)],
				     &compat_fn, sizeof(compat_fn),
				     KSU_PATCH_TEXT_FLUSH_DCACHE);
		if (ret) {
			restore_ret = ksu_patch_text(&live_syscall_table[nr],
						    &previous, sizeof(previous),
						    KSU_PATCH_TEXT_FLUSH_DCACHE);
			if (restore_ret)
				pr_err("syscall table: rollback of %d failed: %d\n",
				       nr, restore_ret);
		}
	}
#endif
	if (ret)
		pr_err("syscall table: failed to patch %d: %d\n", nr, ret);
	return ret;
}

#define KSU_TABLE_WRAPPER(name) \
static long __nocfi table_##name(const struct pt_regs *regs) \
{ \
	return syscall_hooks[__NR_##name](__NR_##name, regs); \
}

KSU_TABLE_WRAPPER(execve)
KSU_TABLE_WRAPPER(execveat)
KSU_TABLE_WRAPPER(newfstatat)
KSU_TABLE_WRAPPER(faccessat)

static syscall_fn_t event_wrapper(int nr)
{
	switch (nr) {
	case __NR_execve:
		return table_execve;
	case __NR_execveat:
		return table_execveat;
	case __NR_newfstatat:
		return table_newfstatat;
	case __NR_faccessat:
		return table_faccessat;
	default:
		return NULL;
	}
}

int ksu_register_syscall_hook(int nr, ksu_syscall_hook_fn fn)
{
	syscall_fn_t wrapper = event_wrapper(nr);
	int ret;

	if (!ksu_syscall_table || !wrapper || !fn)
		return -EINVAL;

	mutex_lock(&syscall_table_lock);
	if (patched_syscalls[nr]) {
		ret = -EBUSY;
		goto out;
	}

	/* Publish the handler before any CPU can enter its wrapper. */
	WRITE_ONCE(syscall_hooks[nr], fn);
	ret = patch_syscall(nr, wrapper);
	if (!ret) {
		patched_syscalls[nr] = true;
		pr_info("syscall table: installed hook for %d\n", nr);
	}
out:
	mutex_unlock(&syscall_table_lock);
	return ret;
}

void ksu_syscall_table_hook(int nr, syscall_fn_t fn, syscall_fn_t *old)
{
	if (!ksu_syscall_table || nr < 0 || nr >= __NR_syscalls)
		return;

	mutex_lock(&syscall_table_lock);
	if (patched_syscalls[nr]) {
		pr_err("syscall table: %d is already hooked\n", nr);
		goto out;
	}
	if (old)
		*old = ksu_syscall_table[nr];
	if (!patch_syscall(nr, fn)) {
		patched_syscalls[nr] = true;
		pr_info("syscall table: installed hook for %d\n", nr);
	}
out:
	mutex_unlock(&syscall_table_lock);
}

void ksu_syscall_table_unhook(int nr)
{
	if (!ksu_syscall_table || nr < 0 || nr >= __NR_syscalls)
		return;

	mutex_lock(&syscall_table_lock);
	if (patched_syscalls[nr] &&
	    !patch_syscall(nr, ksu_syscall_table[nr]))
		patched_syscalls[nr] = false;
	mutex_unlock(&syscall_table_lock);
}

void ksu_unregister_syscall_hook(int nr)
{
	ksu_syscall_table_unhook(nr);
}

bool ksu_has_syscall_hook(int nr)
{
	return nr >= 0 && nr < __NR_syscalls && READ_ONCE(patched_syscalls[nr]);
}

void __init ksu_syscall_hook_init(void)
{
	live_syscall_table = (syscall_fn_t *)sys_call_table;
	ksu_syscall_table = kmemdup(sys_call_table, sizeof(syscall_fn_t) * __NR_syscalls,
				    GFP_KERNEL);
	if (!ksu_syscall_table) {
		pr_err("syscall table: failed to copy original handlers\n");
		return;
	}
#ifdef CONFIG_COMPAT
	{
		int nr;

		for (nr = 0; nr < __NR_syscalls; nr++) {
			int compat_nr = compat_syscall_nr(nr);

			if (compat_nr >= 0)
				compat_original[nr] = compat_sys_call_table[compat_nr];
		}
	}
#endif
}

void __exit ksu_syscall_hook_exit(void)
{
	int nr;

	for (nr = 0; nr < __NR_syscalls; nr++)
		ksu_syscall_table_unhook(nr);
	/* Built-in wrappers can still be in flight on another CPU. */
}
