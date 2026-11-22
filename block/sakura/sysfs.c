// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/stddef.h>

#include "sakura.h"

static ssize_t sakura_show_value(struct elevator_queue *eq, char *page,
				 size_t offset)
{
	struct sakura_data *sd = eq->elevator_data;
	u32 *value = (u32 *)((char *)sd + offset);

	return scnprintf(page, PAGE_SIZE, "%u\n", READ_ONCE(*value));
}

static ssize_t sakura_store_value(struct elevator_queue *eq, const char *page,
				  size_t count, size_t offset, u32 low, u32 high)
{
	struct sakura_data *sd = eq->elevator_data;
	u32 *value = (u32 *)((char *)sd + offset);
	unsigned long flags;
	u32 parsed;
	int ret;

	ret = kstrtou32(page, 10, &parsed);
	if (ret)
		return ret;
	if (parsed < low || parsed > high)
		return -ERANGE;

	spin_lock_irqsave(&sd->lock, flags);
	WRITE_ONCE(*value, parsed);
	if (offset == offsetof(struct sakura_data, adaptive) ||
	    offset == offsetof(struct sakura_data, read_target_us)) {
		WRITE_ONCE(sd->generation, sd->generation + 1);
		WRITE_ONCE(sd->background_share, SAKURA_FULL_SHARE);
	}
	spin_unlock_irqrestore(&sd->lock, flags);
	sakura_wake_queue(sd);
	return count;
}

#define SAKURA_TUNABLE(name, field, low, high) \
enum { name##_offset = offsetof(struct sakura_data, field) }; \
static ssize_t name##_show(struct elevator_queue *eq, char *page) \
{ \
	return sakura_show_value(eq, page, name##_offset); \
} \
static ssize_t name##_store(struct elevator_queue *eq, const char *page, size_t count) \
{ \
	return sakura_store_value(eq, page, count, \
				  name##_offset, low, high); \
}

SAKURA_TUNABLE(adaptive, adaptive, 0, 1);
SAKURA_TUNABLE(read_target_us, read_target_us, 1, 1000000);
SAKURA_TUNABLE(read_burst_kb, read_burst_kb, 4, 4096);
SAKURA_TUNABLE(rt_expire_ms, lanes[SAKURA_RT].expire_ms, 1, 60000);
SAKURA_TUNABLE(read_expire_ms, lanes[SAKURA_READ].expire_ms, 1, 60000);
SAKURA_TUNABLE(sync_write_expire_ms, lanes[SAKURA_SYNC_WRITE].expire_ms, 1, 60000);
SAKURA_TUNABLE(write_expire_ms, lanes[SAKURA_BACKGROUND].expire_ms, 1, 60000);
SAKURA_TUNABLE(maintenance_expire_ms, lanes[SAKURA_MAINTENANCE].expire_ms, 1, 60000);
SAKURA_TUNABLE(idle_expire_ms, lanes[SAKURA_IDLE].expire_ms, 1, 60000);

static ssize_t stats_show(struct elevator_queue *eq, char *page)
{
	struct sakura_data *sd = eq->elevator_data;
	unsigned long flags;
	ssize_t count;

	spin_lock_irqsave(&sd->lock, flags);
	count = scnprintf(page, PAGE_SIZE,
			  "queued %u\nreads_outstanding %d\nwrites_outstanding %d\n"
			  "background_inflight %d\nbackground_share %u/%u\n"
			  "dispatched_rt %llu\ndispatched_read %llu\n"
			  "dispatched_sync_write %llu\ndispatched_background %llu\n"
			  "dispatched_maintenance %llu\ndispatched_idle %llu\n"
			  "insert_merged %llu\ngap_dispatches %llu\n"
			  "expired %llu\nlimited %llu\nadjustments %llu\n",
			  sd->queued, atomic_read(&sd->reads), atomic_read(&sd->writes),
			  atomic_read(&sd->background_inflight), sd->background_share,
			  SAKURA_FULL_SHARE, sd->dispatched[SAKURA_RT],
			  sd->dispatched[SAKURA_READ], sd->dispatched[SAKURA_SYNC_WRITE],
			  sd->dispatched[SAKURA_BACKGROUND], sd->dispatched[SAKURA_MAINTENANCE],
			  sd->dispatched[SAKURA_IDLE], sd->insert_merged,
			  sd->gap_dispatches, sd->expired,
			  sd->limited, sd->adjustments);
	spin_unlock_irqrestore(&sd->lock, flags);
	return count;
}

#define SAKURA_ATTR(name) __ATTR(name, 0644, name##_show, name##_store)

struct elv_fs_entry sakura_attrs[] = {
	SAKURA_ATTR(adaptive),
	SAKURA_ATTR(read_target_us),
	SAKURA_ATTR(read_burst_kb),
	SAKURA_ATTR(rt_expire_ms),
	SAKURA_ATTR(read_expire_ms),
	SAKURA_ATTR(sync_write_expire_ms),
	SAKURA_ATTR(write_expire_ms),
	SAKURA_ATTR(maintenance_expire_ms),
	SAKURA_ATTR(idle_expire_ms),
	__ATTR_RO(stats),
	__ATTR_NULL,
};
