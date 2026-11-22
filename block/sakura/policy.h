/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SAKURA_POLICY_H
#define SAKURA_POLICY_H

#define SAKURA_FULL_SHARE 8U
#define SAKURA_MIN_SAMPLES 32U

/* Multiplication is widened before scaling, including for deep tag maps. */
static inline u32 sakura_depth_limit(u32 depth, u32 share)
{
	u32 limit = (u64)depth * share / SAKURA_FULL_SHARE;

	return limit ? limit : 1;
}

/* Halve on persistent misses, recover gradually with ample latency headroom. */
static inline u32 sakura_share_update(u32 share, u32 samples, u32 late,
				      u64 total_ns, u64 target_ns)
{
	if (samples < SAKURA_MIN_SAMPLES)
		return share;
	if ((u64)late * 10 > samples)
		return share > 1 ? share / 2 : 1;
	if ((u64)late * 100 <= samples && total_ns / samples < target_ns * 3 / 4)
		return share < SAKURA_FULL_SHARE ? share + 1 : share;
	return share;
}

#endif /* SAKURA_POLICY_H */
