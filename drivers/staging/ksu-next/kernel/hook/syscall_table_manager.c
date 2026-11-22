#include <asm/unistd.h>
#include "hook/syscall_hook_manager.h"
#include "hook/syscall_event_bridge.h"
#include "hook/syscall_hook.h"
#include "hook/setuid_hook.h"
#include "feature/sucompat.h"

void __init ksu_syscall_hook_manager_init(void)
{
	ksu_register_syscall_hook(__NR_execve, ksu_hook_execve);
	ksu_register_syscall_hook(__NR_execveat, ksu_hook_execveat);
	ksu_register_syscall_hook(__NR_newfstatat, ksu_hook_newfstatat);
	ksu_register_syscall_hook(__NR_faccessat, ksu_hook_faccessat);
	ksu_setuid_hook_init();
	ksu_sucompat_init();
	ksu_avc_spoof_init();
}

void __exit ksu_syscall_hook_manager_exit(void)
{
	ksu_syscall_hook_exit();
	ksu_avc_spoof_exit();
	ksu_sucompat_exit();
	ksu_setuid_hook_exit();
}
