/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_MM_INLINE_H
#define LINUX_MM_INLINE_H

#include <linux/mm.h>
#include <linux/huge_mm.h>
#include <linux/swap.h>

/**
 * page_is_file_cache - should the page be on a file LRU or anon LRU?
 * @page: the page to test
 *
 * Returns 1 if @page is page cache page backed by a regular filesystem,
 * or 0 if @page is anonymous, tmpfs or otherwise ram or swap backed.
 * Used by functions that manipulate the LRU lists, to sort a page
 * onto the right LRU list.
 *
 * We would like to get this info without a page flag, but the state
 * needs to survive until the page is last deleted from the LRU, which
 * could be as far down as __page_cache_release.
 */
static inline int page_is_file_cache(struct page *page)
{
	return !PageSwapBacked(page);
}

static __always_inline void __update_lru_size(struct lruvec *lruvec,
				enum lru_list lru, enum zone_type zid,
				long nr_pages)
{
	struct pglist_data *pgdat = lruvec_pgdat(lruvec);

	WARN_ON_ONCE(nr_pages != (int)nr_pages);

	mod_lruvec_state(lruvec, NR_LRU_BASE + lru, nr_pages);
	mod_zone_page_state(&pgdat->node_zones[zid],
				NR_ZONE_LRU_BASE + lru, nr_pages);
}

static __always_inline void update_lru_size(struct lruvec *lruvec,
				enum lru_list lru, enum zone_type zid,
				int nr_pages)
{
	__update_lru_size(lruvec, lru, zid, nr_pages);
#ifdef CONFIG_MEMCG
	mem_cgroup_update_lru_size(lruvec, lru, zid, nr_pages);
#endif
}

/**
 * page_lru_base_type - which LRU list type should a page be on?
 * @page: the page to test
 *
 * Used for LRU list index arithmetic.
 *
 * Returns the base LRU type - file or anon - @page should be on.
 */
static inline enum lru_list page_lru_base_type(struct page *page)
{
	if (page_is_file_cache(page))
		return LRU_INACTIVE_FILE;
	return LRU_INACTIVE_ANON;
}

/**
 * __clear_page_lru_flags - clear page lru flags before releasing a page
 * @page: the page that was on lru and now has a zero reference
 */
static __always_inline void __clear_page_lru_flags(struct page *page)
{
	VM_BUG_ON_PAGE(!PageLRU(page), page);

	__ClearPageLRU(page);

	/* this shouldn't happen, so leave the flags to bad_page() */
	if (PageActive(page) && PageUnevictable(page))
		return;

	__ClearPageActive(page);
	__ClearPageUnevictable(page);
}

/**
 * page_lru - which LRU list should a page be on?
 * @page: the page to test
 *
 * Returns the LRU list a page should be on, as an index
 * into the array of LRU lists.
 */
static __always_inline enum lru_list page_lru(struct page *page)
{
	enum lru_list lru;

	VM_BUG_ON_PAGE(PageActive(page) && PageUnevictable(page), page);

	if (PageUnevictable(page))
		lru = LRU_UNEVICTABLE;
	else {
		lru = page_lru_base_type(page);
		if (PageActive(page))
			lru += LRU_ACTIVE;
	}
	return lru;
}

static inline bool vma_has_recency(struct vm_area_struct *vma)
{
	if (vma->vm_flags & (VM_SEQ_READ | VM_RAND_READ))
		return false;

	if (vma->vm_file && (vma->vm_file->f_mode & FMODE_NOREUSE))
		return false;

	return true;
}

/* The two low bits also preserve independent classic-LRU flags. */
static inline unsigned int lru_get_refs_flags(unsigned long flags)
{
	return (!!(flags & BIT(PG_referenced))) |
	       (!!(flags & BIT(PG_workingset)) << 1) |
	       (((flags & LRU_REFS_MASK) >> LRU_REFS_PGOFF) << 2);
}

static inline void lru_set_refs_flags(unsigned long *flags, unsigned int refs)
{
	VM_WARN_ON_ONCE(refs > LRU_REFS_MAX);
	*flags &= ~LRU_REFS_FLAGS;
	if (refs & 1)
		*flags |= BIT(PG_referenced);
	if (refs & 2)
		*flags |= BIT(PG_workingset);
	if (LRU_REFS_WIDTH)
		*flags |= ((unsigned long)refs >> 2) << LRU_REFS_PGOFF;
}

static inline int lru_get_gen_flags(unsigned long flags)
{
	return (int)((flags & LRU_GEN_MASK) >> LRU_GEN_PGOFF) - 1;
}

static inline unsigned int page_lru_refs(const struct page *page)
{
	return lru_get_refs_flags(READ_ONCE(page->flags));
}

static inline bool lru_refs_is_workingset(unsigned int refs)
{
	return refs >= LRU_REFS_WORKINGSET;
}

static inline bool lru_refs_is_active(unsigned int refs)
{
	return refs >= LRU_REFS_ACTIVATED;
}

/* Only for isolated/new pages: on-list changes require counter updates. */
static inline void __page_set_lru_refs(struct page *page, unsigned int refs)
{
	unsigned long old_flags, new_flags;

	do {
		old_flags = READ_ONCE(page->flags);
		VM_WARN_ON_ONCE(lru_get_gen_flags(old_flags) >= 0);
		new_flags = old_flags;
		lru_set_refs_flags(&new_flags, refs);
		if (old_flags == new_flags)
			break;
	} while (cmpxchg(&page->flags, old_flags, new_flags) != old_flags);
}

static inline void page_migrate_lru_refs(struct page *new, struct page *old)
{
	VM_WARN_ON_ONCE_PAGE(PageLRU(new), new);
	VM_WARN_ON_ONCE_PAGE(PageLRU(old), old);
	__page_set_lru_refs(new, page_lru_refs(old));
}

#ifdef CONFIG_LRU_GEN

#ifdef CONFIG_LRU_GEN_ENABLED
static inline bool lru_gen_enabled(void)
{
	DECLARE_STATIC_KEY_TRUE(lru_gen_caps[NR_LRU_GEN_CAPS]);

	return static_branch_likely(&lru_gen_caps[LRU_GEN_CORE]);
}
#else
static inline bool lru_gen_enabled(void)
{
	DECLARE_STATIC_KEY_FALSE(lru_gen_caps[NR_LRU_GEN_CAPS]);

	return static_branch_unlikely(&lru_gen_caps[LRU_GEN_CORE]);
}
#endif

static inline bool lru_gen_in_fault(void)
{
	return current->in_lru_fault;
}

static inline int lru_gen_from_seq(unsigned long seq)
{
	return seq % MAX_NR_GENS;
}

static inline int lru_hist_from_seq(unsigned long seq)
{
	return seq % NR_HIST_GENS;
}

static inline int lru_tier_from_refs(unsigned int refs)
{
	BUILD_BUG_ON(BIT(LRU_TIER_MAX) < LRU_REFS_MAX);
	VM_WARN_ON_ONCE(refs > LRU_REFS_MAX);
	return refs < LRU_REFS_WORKINGSET ? 0 : fls(refs - 1);
}

void page_inc_lru_refs(struct page *page, unsigned int flags);
bool page_reset_lru_refs(struct page *page);

static inline bool lru_gen_is_old(struct lruvec *lruvec, int type, int gen)
{
	unsigned long seq = READ_ONCE(lruvec->lrugen.min_seq[type]);

	return gen == lru_gen_from_seq(seq) || gen == lru_gen_from_seq(seq + 1);
}

static inline void lru_set_gen_flags(unsigned long *flags, int gen)
{
	*flags = (*flags & ~LRU_GEN_MASK) | ((gen + 1UL) << LRU_GEN_PGOFF);
}

static inline int page_lru_gen(struct page *page)
{
	unsigned long flags = READ_ONCE(page->flags);

	return ((flags & LRU_GEN_MASK) >> LRU_GEN_PGOFF) - 1;
}

static inline void lru_gen_update_size(struct lruvec *lruvec, int type,
				       struct page *page, int old_gen, int new_gen)
{
	int zone = page_zonenum(page);
	long delta = hpage_nr_pages(page);
	struct lru_gen_struct *lrugen = &lruvec->lrugen;

	VM_WARN_ON_ONCE(old_gen < 0 && new_gen < 0);
	if (old_gen >= 0)
		atomic_long_sub(delta, &lrugen->nr_pages[old_gen][type][zone]);
	if (new_gen >= 0)
		atomic_long_add(delta, &lrugen->nr_pages[new_gen][type][zone]);
}

static inline unsigned long lru_gen_page_seq(struct lruvec *lruvec,
					   struct page *page, bool reclaiming)
{
	int age;
	int type = page_is_file_cache(page);
	bool workingset = lru_refs_is_workingset(page_lru_refs(page));
	struct lru_gen_struct *lrugen = &lruvec->lrugen;

	/* Place pages relative to max_seq, bounded by the oldest live generation. */
	if (PageActive(page))
		age = MIN_NR_GENS - workingset;
	else if (reclaiming)
		age = MAX_NR_GENS;
	else if ((!type && !PageSwapCache(page)) ||
		 (PageReclaim(page) && (PageDirty(page) || PageWriteback(page))))
		age = MIN_NR_GENS;
	else
		age = MAX_NR_GENS - workingset;
	return max(READ_ONCE(lrugen->max_seq) - age + 1,
		   READ_ONCE(lrugen->min_seq[type]));
}

static inline bool lru_gen_add_page(struct lruvec *lruvec, struct page *page, bool reclaiming)
{
	unsigned long seq;
	unsigned long flags;
	int gen = page_lru_gen(page);
	int type = page_is_file_cache(page);
	int zone = page_zonenum(page);
	struct lru_gen_struct *lrugen = &lruvec->lrugen;

	VM_WARN_ON_ONCE_PAGE(gen != -1, page);

	if (PageUnevictable(page) || !lrugen->enabled)
		return false;
	seq = lru_gen_page_seq(lruvec, page, reclaiming);

	gen = lru_gen_from_seq(seq);
	flags = (gen + 1UL) << LRU_GEN_PGOFF;
	/* see the comment on MIN_NR_GENS about PG_active */
	flags = set_mask_bits(&page->flags, LRU_GEN_MASK | BIT(PG_active), flags);

	lru_gen_update_size(lruvec, type, page, -1, gen);
	__update_lru_size(lruvec, type * LRU_INACTIVE_FILE +
			  lru_refs_is_active(lru_get_refs_flags(flags)),
			  zone, hpage_nr_pages(page));
	/* for rotate_reclaimable_page() */
	if (reclaiming)
		list_add_tail(&page->lru, &lrugen->lists[gen][type][zone]);
	else
		list_add(&page->lru, &lrugen->lists[gen][type][zone]);

	return true;
}

static inline bool lru_gen_del_page(struct lruvec *lruvec, struct page *page, bool reclaiming)
{
	unsigned long flags;
	int gen = page_lru_gen(page);

	if (gen < 0)
		return false;

	VM_WARN_ON_ONCE_PAGE(PageActive(page), page);
	VM_WARN_ON_ONCE_PAGE(PageUnevictable(page), page);

	/* Preserve refs across isolation; PG_active retains recent placement. */
	flags = set_mask_bits(&page->flags, LRU_GEN_MASK, 0);
	gen = lru_get_gen_flags(flags);
	if (!reclaiming &&
	    ((READ_ONCE(lruvec->lrugen.max_seq) - gen) % MAX_NR_GENS) < MIN_NR_GENS)
		SetPageActive(page);
	lru_gen_update_size(lruvec, page_is_file_cache(page), page, gen, -1);
	__update_lru_size(lruvec, page_is_file_cache(page) * LRU_INACTIVE_FILE +
			  lru_refs_is_active(lru_get_refs_flags(flags)),
			  page_zonenum(page), -hpage_nr_pages(page));
	list_del(&page->lru);

	return true;
}

#else /* !CONFIG_LRU_GEN */

static inline void page_inc_lru_refs(struct page *page, unsigned int flags)
{
	WARN_ON_ONCE(1);
}

static inline bool page_reset_lru_refs(struct page *page)
{
	return false;
}

static inline bool lru_gen_enabled(void)
{
	return false;
}

static inline bool lru_gen_in_fault(void)
{
	return false;
}

static inline bool lru_gen_add_page(struct lruvec *lruvec, struct page *page, bool reclaiming)
{
	return false;
}

static inline bool lru_gen_del_page(struct lruvec *lruvec, struct page *page, bool reclaiming)
{
	return false;
}

#endif /* CONFIG_LRU_GEN */

static inline void page_inc_lru_refs_fast(struct page *page)
{
	unsigned long old_flags, new_flags;

	page = compound_head(page);
	if (!lru_gen_enabled() && lru_get_gen_flags(READ_ONCE(page->flags)) < 0) {
		SetPageReferenced(page);
		return;
	}
	do {
		old_flags = READ_ONCE(page->flags);
		if (lru_get_refs_flags(old_flags))
			break;
		new_flags = old_flags;
		lru_set_refs_flags(&new_flags, LRU_REFS_REFERENCED);
	} while (cmpxchg(&page->flags, old_flags, new_flags) != old_flags);
}

static inline bool page_check_lru_refs(struct page *page)
{
	return lru_gen_enabled() || lru_get_gen_flags(READ_ONCE(page->flags)) >= 0 ?
		page_lru_refs(page) != 0 : PageReferenced(page);
}

static inline void page_clear_lru_refs(struct page *page)
{
	if (lru_gen_enabled() || lru_get_gen_flags(READ_ONCE(page->flags)) >= 0)
		page_reset_lru_refs(page);
	else
		ClearPageReferenced(page);
}

/* PG_workingset is only bit 1 of the FG count, not its classification. */
static inline bool page_test_workingset(const struct page *page)
{
	return lru_refs_is_workingset(page_lru_refs(page));
}

static __always_inline void add_page_to_lru_list(struct page *page,
				struct lruvec *lruvec)
{
	enum lru_list lru = page_lru(page);

	if (lru_gen_add_page(lruvec, page, false))
		return;

	update_lru_size(lruvec, lru, page_zonenum(page), hpage_nr_pages(page));
	list_add(&page->lru, &lruvec->lists[lru]);
}

static __always_inline void add_page_to_lru_list_tail(struct page *page,
				struct lruvec *lruvec)
{
	enum lru_list lru = page_lru(page);

	if (lru_gen_add_page(lruvec, page, true))
		return;

	update_lru_size(lruvec, lru, page_zonenum(page), hpage_nr_pages(page));
	list_add_tail(&page->lru, &lruvec->lists[lru]);
}

static __always_inline void del_page_from_lru_list(struct page *page,
				struct lruvec *lruvec)
{
	if (lru_gen_del_page(lruvec, page, false))
		return;

	list_del(&page->lru);
	update_lru_size(lruvec, page_lru(page), page_zonenum(page),
			-hpage_nr_pages(page));
}

#endif
