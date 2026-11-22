/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SAKURA_H
#define SAKURA_H

#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/elevator.h>
#include <linux/percpu.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>

#include "policy.h"

#define SAKURA_RQ_BACKGROUND 1UL
#define SAKURA_RQ_SAMPLE 2UL

/* Recovery insertion uses a separate list and is not subject to this order. */
enum sakura_lane_id {
	SAKURA_RT,
	SAKURA_READ,
	SAKURA_SYNC_WRITE,
	SAKURA_BACKGROUND,
	SAKURA_MAINTENANCE,
	SAKURA_IDLE,
	SAKURA_LANES,
};

struct sakura_lane {
	struct rb_root sectors;
	struct list_head fifo;
	sector_t next_sector;
	u32 expire_ms;
};

struct sakura_samples {
	u64 first_ns;
	u64 total_ns;
	u32 count;
	u32 late;
	u32 generation;
};

struct sakura_data {
	struct request_queue *queue;
	/* Protects lane membership, dispatch policy and controller settings. */
	spinlock_t lock;
	struct sakura_lane lanes[SAKURA_LANES];
	struct list_head recovery;
	u32 queued;
	u32 read_bytes;
	u32 read_burst_kb;
	u32 read_target_us;
	u32 adaptive;
	u32 generation;
	u32 background_share;
	atomic_t reads;
	atomic_t writes;
	atomic_t background_inflight;
	atomic_t blocked;
	struct sakura_samples __percpu *samples;
	u64 dispatched[SAKURA_LANES];
	u64 insert_merged;
	u64 gap_dispatches;
	u64 expired;
	u64 limited;
	u64 adjustments;
};

static inline enum sakura_lane_id sakura_rq_lane(const struct request *rq)
{
	return (unsigned long)rq->elv.priv[0];
}

void sakura_prepare_request(struct request *rq, struct bio *bio);
void sakura_finish_request(struct request *rq);
void sakura_completed_request(struct request *rq, u64 now);
void sakura_requeue_request(struct request *rq);
void sakura_limit_depth(unsigned int opf, struct blk_mq_alloc_data *data);
void sakura_depth_updated(struct blk_mq_hw_ctx *hctx);
bool sakura_background_allowed(struct sakura_data *sd,
			       struct blk_mq_hw_ctx *hctx);
void sakura_wake_queue(struct sakura_data *sd);

extern struct elv_fs_entry sakura_attrs[];

#endif /* SAKURA_H */
