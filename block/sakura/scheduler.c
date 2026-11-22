// SPDX-License-Identifier: GPL-2.0-only
/* sakura: bounded read preference with completion-driven write admission. */
#include <linux/bio.h>
#include <linux/init.h>
#include <linux/ioprio.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "sakura.h"
#include "../blk-mq-sched.h"
#include "../blk.h"

static const u32 sakura_expire_ms[SAKURA_LANES] = {
	[SAKURA_RT] = 4,
	[SAKURA_READ] = 8,
	[SAKURA_SYNC_WRITE] = 16,
	[SAKURA_BACKGROUND] = 100,
	[SAKURA_MAINTENANCE] = 500,
	[SAKURA_IDLE] = 200,
};

static enum sakura_lane_id sakura_classify(unsigned int opf, u16 ioprio)
{
	unsigned int op = opf & REQ_OP_MASK;

	if (IOPRIO_PRIO_CLASS(ioprio) == IOPRIO_CLASS_IDLE)
		return SAKURA_IDLE;
	if (IOPRIO_PRIO_CLASS(ioprio) == IOPRIO_CLASS_RT)
		return SAKURA_RT;
	if (op == REQ_OP_READ)
		return SAKURA_READ;
	if (op == REQ_OP_WRITE)
		return opf & REQ_SYNC ? SAKURA_SYNC_WRITE : SAKURA_BACKGROUND;
	return SAKURA_MAINTENANCE;
}

static struct request *sakura_oldest(struct sakura_lane *lane)
{
	return list_first_entry_or_null(&lane->fifo, struct request, queuelist);
}

static void sakura_remove_request(struct sakura_data *sd, struct request *rq)
{
	list_del_init(&rq->queuelist);
	if (!RB_EMPTY_NODE(&rq->rb_node))
		elv_rb_del(&sd->lanes[sakura_rq_lane(rq)].sectors, rq);
	elv_rqhash_del(sd->queue, rq);
	if (sd->queue->last_merge == rq)
		sd->queue->last_merge = NULL;
	WRITE_ONCE(sd->queued, sd->queued - 1);
}

static bool sakura_allow_merge(struct request_queue *q, struct request *rq,
			       struct bio *bio)
{
	return sakura_classify(rq->cmd_flags, req_get_ioprio(rq)) ==
		sakura_classify(bio->bi_opf, bio->bi_ioprio);
}

static int sakura_request_merge(struct request_queue *q, struct request **rq,
				struct bio *bio)
{
	struct sakura_data *sd = q->elevator->elevator_data;
	enum sakura_lane_id id = sakura_classify(bio->bi_opf, bio->bi_ioprio);
	struct request *candidate;

	candidate = elv_rb_find(&sd->lanes[id].sectors, bio_end_sector(bio));
	if (!candidate || !elv_bio_merge_ok(candidate, bio))
		return ELEVATOR_NO_MERGE;

	*rq = candidate;
	return blk_discard_mergable(candidate) ? ELEVATOR_DISCARD_MERGE :
		ELEVATOR_FRONT_MERGE;
}

static void sakura_request_merged(struct request_queue *q, struct request *rq,
				  enum elv_merge type)
{
	struct sakura_data *sd = q->elevator->elevator_data;
	struct rb_root *root = &sd->lanes[sakura_rq_lane(rq)].sectors;

	if (type != ELEVATOR_FRONT_MERGE || RB_EMPTY_NODE(&rq->rb_node))
		return;
	elv_rb_del(root, rq);
	elv_rb_add(root, rq);
}

static void sakura_requests_merged(struct request_queue *q, struct request *rq,
				   struct request *next)
{
	struct sakura_data *sd = q->elevator->elevator_data;

	/* An insertion merge consumes an incoming request not yet on our lists. */
	if (RB_EMPTY_NODE(&next->rb_node))
		return;

	/* Keep the age and FIFO position of the oldest constituent request. */
	if (time_before64(next->fifo_time, rq->fifo_time)) {
		list_move(&rq->queuelist, &next->queuelist);
		rq->fifo_time = next->fifo_time;
	}
	sakura_remove_request(sd, next);
}

static bool sakura_bio_merge(struct request_queue *q, struct bio *bio,
			     unsigned int nr_segs)
{
	struct sakura_data *sd = q->elevator->elevator_data;
	struct request *free = NULL;
	bool merged;
	unsigned long flags;

	spin_lock_irqsave(&sd->lock, flags);
	merged = blk_mq_sched_try_merge(q, bio, nr_segs, &free);
	spin_unlock_irqrestore(&sd->lock, flags);
	if (free)
		blk_mq_free_request(free);
	return merged;
}

/* Merge once within a lane; never recursively consume another queued request. */
static bool sakura_insert_merge(struct sakura_data *sd, struct request *rq,
				struct list_head *free)
{
	struct request_queue *q = sd->queue;
	struct request *candidate = q->last_merge;

	/* Multi-range discard does not call the request-merge callback in 5.4. */
	if (blk_queue_nomerges(q) || req_op(rq) == REQ_OP_DISCARD)
		return false;
	if (candidate && rq->bio && !RB_EMPTY_NODE(&candidate->rb_node) &&
	    sakura_rq_lane(candidate) == sakura_rq_lane(rq) &&
	    blk_rq_merge_ok(candidate, rq->bio) &&
	    blk_attempt_req_merge(q, candidate, rq))
		goto merged;
	if (blk_queue_noxmerges(q))
		return false;
	candidate = elv_rqhash_find(q, blk_rq_pos(rq));
	if (!candidate || !rq->bio ||
	    sakura_rq_lane(candidate) != sakura_rq_lane(rq) ||
	    !blk_rq_merge_ok(candidate, rq->bio) ||
	    !blk_attempt_req_merge(q, candidate, rq))
		return false;
merged:
	list_add_tail(&rq->queuelist, free);
	sd->insert_merged++;
	return true;
}

static void sakura_insert_requests(struct blk_mq_hw_ctx *hctx,
				   struct list_head *list, bool at_head)
{
	struct request_queue *q = hctx->queue;
	struct sakura_data *sd = q->elevator->elevator_data;
	unsigned long flags;
	LIST_HEAD(free);

	spin_lock_irqsave(&sd->lock, flags);
	while (!list_empty(list)) {
		struct request *rq = list_first_entry(list, struct request, queuelist);
		enum sakura_lane_id id;
		struct sakura_lane *lane;

		list_del_init(&rq->queuelist);
		if (at_head || blk_rq_is_passthrough(rq) ||
		    !(rq->rq_flags & RQF_ELVPRIV)) {
			RB_CLEAR_NODE(&rq->rb_node);
			if (at_head)
				list_add(&rq->queuelist, &sd->recovery);
			else
				list_add_tail(&rq->queuelist, &sd->recovery);
			WRITE_ONCE(sd->queued, sd->queued + 1);
			continue;
		}

		id = sakura_classify(rq->cmd_flags, req_get_ioprio(rq));
		rq->elv.priv[0] = (void *)(unsigned long)id;
		if (sakura_insert_merge(sd, rq, &free))
			continue;

		lane = &sd->lanes[id];
		blk_mq_sched_request_inserted(rq);
		elv_rb_add(&lane->sectors, rq);
		if (rq_mergeable(rq)) {
			elv_rqhash_add(q, rq);
			if (!q->last_merge)
				q->last_merge = rq;
		}
		rq->fifo_time = get_jiffies_64() + msecs_to_jiffies(lane->expire_ms);
		list_add_tail(&rq->queuelist, &lane->fifo);
		WRITE_ONCE(sd->queued, sd->queued + 1);
	}
	spin_unlock_irqrestore(&sd->lock, flags);
	/* finish_request may wake queues and must run outside the scheduler lock. */
	blk_mq_free_requests(&free);
}

static struct request *sakura_expired_request(struct sakura_data *sd)
{
	struct request *oldest = NULL;
	unsigned int i;

	for (i = 0; i < SAKURA_LANES; i++) {
		struct request *rq = sakura_oldest(&sd->lanes[i]);

		if (!rq || time_before64(get_jiffies_64(), rq->fifo_time))
			continue;
		if (!oldest || time_before64(rq->fifo_time, oldest->fifo_time))
			oldest = rq;
	}
	return oldest;
}

static struct request *sakura_choose_request(struct sakura_data *sd,
					     struct blk_mq_hw_ctx *hctx)
{
	struct request *read, *write, *background, *rq;

	rq = sakura_expired_request(sd);
	if (rq) {
		sd->expired++;
		return rq;
	}
	rq = sakura_oldest(&sd->lanes[SAKURA_RT]);
	if (rq)
		return rq;

	read = sakura_oldest(&sd->lanes[SAKURA_READ]);
	write = sakura_oldest(&sd->lanes[SAKURA_SYNC_WRITE]);
	background = sakura_oldest(&sd->lanes[SAKURA_BACKGROUND]);
	if (background && !sakura_background_allowed(sd, hctx))
		background = NULL;

	if (read && ((!write && !background) ||
		     sd->read_bytes < sd->read_burst_kb * 1024))
		return read;
	if (write)
		return write;
	if (background)
		return background;
	if (read)
		return read;
	rq = sakura_oldest(&sd->lanes[SAKURA_MAINTENANCE]);
	if (rq)
		return rq;
	rq = sakura_oldest(&sd->lanes[SAKURA_IDLE]);
	if (rq && req_op(rq) == REQ_OP_WRITE &&
	    !sakura_background_allowed(sd, hctx))
		return NULL;
	return rq;
}

static struct request *sakura_next_request(struct sakura_lane *lane)
{
	struct rb_node *node = lane->sectors.rb_node;
	struct request *next = NULL;

	while (node) {
		struct request *rq = rb_entry(node, struct request, rb_node);
		sector_t sector = blk_rq_pos(rq);

		if (sector < lane->next_sector) {
			node = node->rb_right;
		} else {
			next = rq;
			if (sector == lane->next_sector)
				break;
			node = node->rb_left;
		}
	}
	return next;
}

static struct request *sakura_dispatch_request(struct blk_mq_hw_ctx *hctx)
{
	struct sakura_data *sd = hctx->queue->elevator->elevator_data;
	struct request *rq;
	enum sakura_lane_id id;
	unsigned long state = 0;
	unsigned long flags;

	spin_lock_irqsave(&sd->lock, flags);
	rq = list_first_entry_or_null(&sd->recovery, struct request, queuelist);
	if (rq) {
		sakura_remove_request(sd, rq);
		goto out;
	}
	rq = sakura_choose_request(sd, hctx);
	if (!rq)
		goto out;

	id = sakura_rq_lane(rq);
	/* An expired FIFO head takes precedence over locality within its lane. */
	if (time_before64(get_jiffies_64(), rq->fifo_time)) {
		struct request *next = sakura_next_request(&sd->lanes[id]);

		if (next) {
			if (next != rq && blk_rq_pos(next) != sd->lanes[id].next_sector)
				sd->gap_dispatches++;
			rq = next;
		}
	}
	sd->lanes[id].next_sector = blk_rq_pos(rq) + blk_rq_sectors(rq);
	sakura_remove_request(sd, rq);
	sd->dispatched[id]++;
	if (req_op(rq) == REQ_OP_READ) {
		sd->read_bytes = min_t(u64, (u64)sd->read_bytes + blk_rq_bytes(rq),
				       (u64)sd->read_burst_kb * 1024);
		if (blk_rq_bytes(rq) <= 64 * 1024)
			state |= SAKURA_RQ_SAMPLE;
	} else {
		sd->read_bytes = 0;
	}
	if (id == SAKURA_BACKGROUND ||
	    (id == SAKURA_IDLE && req_op(rq) == REQ_OP_WRITE)) {
		atomic_inc(&sd->background_inflight);
		state |= SAKURA_RQ_BACKGROUND;
	}
	rq->elv.priv[1] = (void *)state;
out:
	spin_unlock_irqrestore(&sd->lock, flags);
	return rq;
}

static bool sakura_has_work(struct blk_mq_hw_ctx *hctx)
{
	struct sakura_data *sd = hctx->queue->elevator->elevator_data;

	return READ_ONCE(sd->queued) != 0;
}

static int sakura_init_hctx(struct blk_mq_hw_ctx *hctx, unsigned int index)
{
	sakura_depth_updated(hctx);
	return 0;
}

static int sakura_init_sched(struct request_queue *q, struct elevator_type *type)
{
	struct elevator_queue *eq;
	struct sakura_data *sd;
	unsigned int i;

	if (blk_queue_is_zoned(q))
		return -EOPNOTSUPP;
	eq = elevator_alloc(q, type);
	if (!eq)
		return -ENOMEM;
	sd = kzalloc_node(sizeof(*sd), GFP_KERNEL, q->node);
	if (!sd)
		goto put_eq;
	sd->samples = alloc_percpu_gfp(struct sakura_samples, GFP_KERNEL | __GFP_ZERO);
	if (!sd->samples)
		goto free_data;

	sd->queue = q;
	sd->read_burst_kb = 256;
	sd->read_target_us = 2000;
	sd->adaptive = 1;
	sd->generation = 1;
	sd->background_share = SAKURA_FULL_SHARE;
	spin_lock_init(&sd->lock);
	INIT_LIST_HEAD(&sd->recovery);
	for (i = 0; i < SAKURA_LANES; i++) {
		sd->lanes[i].sectors = RB_ROOT;
		INIT_LIST_HEAD(&sd->lanes[i].fifo);
		sd->lanes[i].expire_ms = sakura_expire_ms[i];
	}
	eq->elevator_data = sd;
	q->elevator = eq;
	return 0;

free_data:
	kfree(sd);
put_eq:
	kobject_put(&eq->kobj);
	return -ENOMEM;
}

static void sakura_exit_sched(struct elevator_queue *eq)
{
	struct sakura_data *sd = eq->elevator_data;
	unsigned int i;

	WARN_ON_ONCE(sd->queued || atomic_read(&sd->background_inflight));
	WARN_ON_ONCE(atomic_read(&sd->reads) || atomic_read(&sd->writes));
	for (i = 0; i < SAKURA_LANES; i++)
		WARN_ON_ONCE(!list_empty(&sd->lanes[i].fifo));
	free_percpu(sd->samples);
	kfree(sd);
}

static struct elevator_type sakura_elevator = {
	.ops = {
		.allow_merge = sakura_allow_merge,
		.bio_merge = sakura_bio_merge,
		.request_merge = sakura_request_merge,
		.request_merged = sakura_request_merged,
		.requests_merged = sakura_requests_merged,
		.insert_requests = sakura_insert_requests,
		.dispatch_request = sakura_dispatch_request,
		.has_work = sakura_has_work,
		.prepare_request = sakura_prepare_request,
		.finish_request = sakura_finish_request,
		.completed_request = sakura_completed_request,
		.requeue_request = sakura_requeue_request,
		.limit_depth = sakura_limit_depth,
		.depth_updated = sakura_depth_updated,
		.init_hctx = sakura_init_hctx,
		.init_sched = sakura_init_sched,
		.exit_sched = sakura_exit_sched,
		.former_request = elv_rb_former_request,
		.next_request = elv_rb_latter_request,
	},
	.elevator_attrs = sakura_attrs,
	.elevator_name = "sakura",
	.elevator_owner = THIS_MODULE,
};

static int __init sakura_init(void)
{
	return elv_register(&sakura_elevator);
}

static void __exit sakura_exit(void)
{
	elv_unregister(&sakura_elevator);
}

module_init(sakura_init);
module_exit(sakura_exit);
MODULE_ALIAS("mq-sakura-iosched");
MODULE_DESCRIPTION("sakura flash I/O scheduler");
MODULE_LICENSE("GPL");
