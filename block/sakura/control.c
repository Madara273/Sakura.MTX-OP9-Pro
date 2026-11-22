// SPDX-License-Identifier: GPL-2.0-only
#include <linux/bio.h>
#include <linux/ioprio.h>
#include <linux/slab.h>

#include "sakura.h"
#include "../blk-mq-sched.h"

#define SAKURA_SAMPLE_WINDOW_NS (20 * NSEC_PER_MSEC)

void sakura_wake_queue(struct sakura_data *sd)
{
	if (atomic_xchg(&sd->blocked, 0))
		blk_mq_run_hw_queues(sd->queue, true);
}

void sakura_prepare_request(struct request *rq, struct bio *bio)
{
	struct sakura_data *sd = rq->q->elevator->elevator_data;

	rq->elv.priv[0] = NULL;
	rq->elv.priv[1] = NULL;
	if (req_op(rq) == REQ_OP_READ)
		atomic_inc(&sd->reads);
	else if (req_op(rq) == REQ_OP_WRITE)
		atomic_inc(&sd->writes);
}

/* Called exactly once per dispatch, including explicit requeue and abort. */
static void sakura_release_dispatch(struct sakura_data *sd, struct request *rq)
{
	if (!(rq->rq_flags & RQF_ELVPRIV))
		return;
	if ((unsigned long)rq->elv.priv[1] & SAKURA_RQ_BACKGROUND)
		atomic_dec(&sd->background_inflight);
	rq->elv.priv[1] = NULL;
}

void sakura_requeue_request(struct request *rq)
{
	struct sakura_data *sd = rq->q->elevator->elevator_data;

	sakura_release_dispatch(sd, rq);
	sakura_wake_queue(sd);
}

void sakura_finish_request(struct request *rq)
{
	struct sakura_data *sd = rq->q->elevator->elevator_data;

	sakura_release_dispatch(sd, rq);
	if (req_op(rq) == REQ_OP_READ)
		atomic_dec(&sd->reads);
	else if (req_op(rq) == REQ_OP_WRITE)
		atomic_dec(&sd->writes);
	sakura_wake_queue(sd);
}

static void sakura_feedback(struct sakura_data *sd,
			    const struct sakura_samples *samples)
{
	unsigned long flags;
	u32 share;

	spin_lock_irqsave(&sd->lock, flags);
	if (sd->adaptive && samples->generation == sd->generation) {
		share = sakura_share_update(sd->background_share, samples->count,
					    samples->late, samples->total_ns,
					    (u64)sd->read_target_us * NSEC_PER_USEC);
		if (share != sd->background_share) {
			WRITE_ONCE(sd->background_share, share);
			sd->adjustments++;
		}
	}
	spin_unlock_irqrestore(&sd->lock, flags);
}

void sakura_completed_request(struct request *rq, u64 now)
{
	struct sakura_data *sd = rq->q->elevator->elevator_data;
	struct sakura_samples *samples, batch;
	unsigned long flags;
	u64 latency;
	bool ready = false;
	bool sample;

	if (!(rq->rq_flags & RQF_ELVPRIV))
		return;
	sample = (unsigned long)rq->elv.priv[1] & SAKURA_RQ_SAMPLE;
	sakura_release_dispatch(sd, rq);
	if (!sample || !READ_ONCE(sd->adaptive) || req_op(rq) != REQ_OP_READ ||
	    !atomic_read(&sd->writes) || !rq->start_time_ns ||
	    now <= rq->start_time_ns)
		goto wake;

	latency = now - rq->start_time_ns;
	/* IRQ exclusion also pins the CPU and serializes its completion samples. */
	local_irq_save(flags);
	samples = this_cpu_ptr(sd->samples);
	if (samples->generation != READ_ONCE(sd->generation) ||
	    (samples->count && now - samples->first_ns > NSEC_PER_SEC)) {
		memset(samples, 0, sizeof(*samples));
		samples->generation = READ_ONCE(sd->generation);
	}
	if (!samples->count)
		samples->first_ns = now;
	samples->count++;
	samples->total_ns += latency;
	if (latency > (u64)READ_ONCE(sd->read_target_us) * NSEC_PER_USEC)
		samples->late++;
	if (samples->count >= SAKURA_MIN_SAMPLES &&
	    now - samples->first_ns >= SAKURA_SAMPLE_WINDOW_NS) {
		batch = *samples;
		memset(samples, 0, sizeof(*samples));
		ready = true;
	}
	local_irq_restore(flags);
	if (ready)
		sakura_feedback(sd, &batch);
wake:
	sakura_wake_queue(sd);
}

bool sakura_background_allowed(struct sakura_data *sd,
			       struct blk_mq_hw_ctx *hctx)
{
	u32 share = READ_ONCE(sd->background_share);
	u32 depth;

	if (!READ_ONCE(sd->adaptive) || share == SAKURA_FULL_SHARE ||
	    !atomic_read(&sd->reads))
		return true;

	depth = hctx->tags->bitmap_tags.sb.depth;
	if (atomic_read(&sd->background_inflight) < sakura_depth_limit(depth, share))
		return true;

	/* Publish before rechecking: a concurrent completion cannot lose a wakeup. */
	atomic_set(&sd->blocked, 1);
	/* Order the blocked flag before testing the completion-side counters. */
	smp_mb();
	if (!atomic_read(&sd->reads) ||
	    atomic_read(&sd->background_inflight) < sakura_depth_limit(depth, share))
		return true;

	blk_mq_sched_mark_restart_hctx(hctx);
	sd->limited++;
	return false;
}

void sakura_limit_depth(unsigned int opf, struct blk_mq_alloc_data *data)
{
	struct sakura_data *sd = data->q->elevator->elevator_data;
	u32 share = READ_ONCE(sd->background_share);

	if (opf & REQ_SYNC || (opf & REQ_OP_MASK) != REQ_OP_WRITE ||
	    !READ_ONCE(sd->adaptive) || share == SAKURA_FULL_SHARE ||
	    !atomic_read(&sd->reads))
		return;

	data->shallow_depth =
		sakura_depth_limit(1U << data->hctx->sched_tags->bitmap_tags.sb.shift,
				   share);
}

void sakura_depth_updated(struct blk_mq_hw_ctx *hctx)
{
	/* The controller can reduce any per-word allocation limit down to one. */
	sbitmap_queue_min_shallow_depth(&hctx->sched_tags->bitmap_tags, 1);
}
