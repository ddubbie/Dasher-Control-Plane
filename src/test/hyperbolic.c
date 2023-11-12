#include <math.h>
#include "hyperbolic.h"
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
//#include "debug.h"

//#include <rte_random.h>
//#include <rte_atomic.h>

typedef struct hyperbolic_cache_s {
	size_t n_items;
	size_t sample_size;
	item **set;
	item *root;
//	rte_spinlock_t sl;
} hyperbolic_cache;

static size_t RandomlySample(hyperbolic_cache *hc);
static void CalculatePrioForOffloading(item *it, uint32_t cur_ts);
static void CalculatePrioForEviction(item *it, uint32_t cur_ts);
static void QuickSort(item **set, size_t left, size_t right);
static void SortSampledSet(hyperbolic_cache *hc);

static void CompleteBinTreeInsert(hyperbolic_cache *hc, item *it);
static void CompleteBinTreeDelete(hyperbolic_cache *hc, item *it);
static item *CompleteBinTreeSearchItem(hyperbolic_cache *hc, const size_t pos);

static item *HyperbolicCacheGetOffloadingCandidate(hyperbolic_cache *hc);
static item *HyperbolicCacheGetEvictionCandidate(hyperbolic_cache *hc);

static item sentinel;
static bool is_prng_set = false;

inline static size_t
RandomlySample(hyperbolic_cache *hc)
{
	size_t i, sample_size;

	//sample_size = RTE_MIN(hc->sample_size, hc->n_items);

	for (i = 0; i < sample_size; i++) {
		size_t pos; //= rte_rand() % hc->sample_size + 1;
		hc->set[i] = CompleteBinTreeSearchItem(hc, pos);
	}
	return sample_size;
}

inline static void
CalculatePrioForOffloading(item *it, uint32_t cur_ts)
{
	/*
	double prio;
	if (it->vv->ts_ms == cur_ts) 
		return;
	prio = (1 - HYPERBOLIC_OMEGA) * (double)(it->vv->n_requests) /
		((it->sv->sz >> 10) * (cur_ts - it->vv->ts_ms)) + HYPERBOLIC_OMEGA * it->vv->prio;
	it->vv->prio = prio;
	it->vv->ts_ms = cur_ts;*/
}

inline static void
CalculatePrioForEviction(item *it, uint32_t cur_ts) 
{
	double prio;
	if (it->vv->ts_ms == cur_ts)
		return;
	prio = (double)it->vv->n_requests / ((it->sv->sz >> 10) * (cur_ts - it->vv->ts_ms));
	it->vv->prio = prio;
	it->vv->ts_ms = cur_ts;
}

inline static void
SortSampledSet(hyperbolic_cache *hc) 
{
	QuickSort(hc->set, 0, hc->sample_size - 1);
}

/* Sort to ascending orer */
inline static void
QuickSort(item **set, size_t left, size_t right)
{
	size_t mid, i, j;
	double pivot;

	if (left >= right) return;

	mid = (left + right) / 2;
	i = left;
	j = right;

	pivot = set[mid]->vv->prio;

	while(i <= j)
	{
		item *temp;
		while (set[i]->vv->prio < pivot && i < right)
			i++;
		while (set[j]->vv->prio > pivot && j > left)
			j--;
		if (i <= j) {
			temp = set[i];
			set[i] = set[j];
			set[j] = temp;
			i++;
			j--;
		}
	}
	QuickSort(set, left, j);
	QuickSort(set, i, right);
}

inline static void
CompleteBinTreeInsert(hyperbolic_cache *hc, item *it) 
{
	if (!hc->root) {
		hc->root = it;
		it->parent = &sentinel;
	} else {
		int i;
		size_t pos = hc->n_items + 1;
		size_t num_edges = log2(pos);
		item *ptr = hc->root;

		for (i = num_edges - 1; i > 0; i--) 
			ptr = (pos & (1LU << i)) ? ptr->right : ptr->left;
		if (pos & 1LU) {
			ptr->right = it;
		} else {
			ptr->left = it;
		}
		it->parent = ptr;
	}
	it->left = &sentinel;
	it->right = &sentinel;
	hc->n_items++;
}

inline static void
CompleteBinTreeDelete(hyperbolic_cache *hc, item *it)
{
	item *s;
	s = CompleteBinTreeSearchItem(hc, hc->n_items);

	printf("%lu %lu\n", it->sv->hv, s->sv->hv);

	if (it == hc->root) {
		if (s == it) { /* Only one node is in the tree */
			assert(hc->n_items == 1);
			hc->root = NULL;
		} else {
			if (s->parent->left == s) {
				s->parent->left = &sentinel;
			} else {
				s->parent->right = &sentinel;
			}

			hc->root = s;
			s->left = it->left;
			s->right = it->right;
			s->parent = &sentinel;

			s->left->parent = s;
			s->right->parent = s;
		}
	} else {
		if (s == it) {
			if (it->parent->left == it) {
				it->parent->left = &sentinel;
			} else {
				it->parent->right = &sentinel;
			}
		} else {
			if (s->parent->left == s) {
				s->parent->left = &sentinel;
			} else {
				s->parent->right = &sentinel;
			}

			if (it->parent->left == it) {
				it->parent->left = s;
			} else {
				it->parent->right = s;
			}

			s->left = it->left;
			s->right = it->right;
			s->parent = it->parent;
			s->left->parent = s;
			s->right->parent = s;
		}
	}

	it->left = NULL;
	it->right = NULL;
	it->parent = NULL;
	hc->n_items--;
}

inline static item *
CompleteBinTreeSearchItem(hyperbolic_cache *hc, const size_t pos)
{

	int num_edges; 
	item *ptr = hc->root;

	if (pos > hc->n_items) {
		//log_error("Wrong postion, pos:%lu, n_items:%lu\n", pos, hc->n_items);
		return NULL;
	}

	if (pos == 1)
		return ptr;
	
	num_edges = log2(pos) - 1;
	for (; num_edges >= 0; num_edges--) {
		ptr = (pos & (1LU << num_edges)) ? ptr->right : ptr->left;
	}

	return ptr;
}

inline static item *
HyperbolicCacheGetOffloadingCandidate(hyperbolic_cache *hc)
{
	size_t i, sample_size;
	struct timespec cur_ts;
	uint32_t cur_ms;

	/* Random Sampling */
	sample_size = RandomlySample(hc);
	//GET_CURRENT_TIMESPEC(&cur_ts);
	//cur_ms = TS_TO_MS(&cur_ts);

	/* Calculate priority */
	for (i = 0; i < sample_size; i++)
		CalculatePrioForOffloading(hc->set[i], cur_ms);

	/* Sort the items by the priorities which were calculated */
	SortSampledSet(hc);
	return hc->set[sample_size - 1];

}

inline static item *
HyperbolicCacheGetEvictionCandidate(hyperbolic_cache *hc)
{
	size_t i, sample_size;
	struct timespec cur_ts;
	uint32_t cur_ms;
	item *eviction_candidate;
	uint16_t st;
	uint32_t loop_count = 3;//HYPERBOLIC_MAX_LOOP_COUNT;

	while (loop_count--) {
		sample_size = RandomlySample(hc);
		//GET_CURRENT_TIMESPEC(&cur_ts);
		//cur_ms = TS_TO_MS(&cur_ts);

		for (i = 0; i < sample_size; i++) 
			CalculatePrioForEviction(hc->set[i], cur_ms);

		SortSampledSet(hc);

		for (i = 0; i < sample_size; i++) {
			eviction_candidate = hc->set[i];
			//st = rte_atomic16_read(&eviction_candidate->state);
			if (st == ITEM_STATE_AT_NIC) {
				return eviction_candidate;
			}
		}
	}

	return NULL;
}

void *
hyperbolic_cache_setup(void *arg)
{
	hyperbolic_cache *hc;

	hc = calloc(1, sizeof(hyperbolic_cache));
	if (!hc) {
		/*
		rte_exit(EXIT_FAILURE,
				"Failed dto allocate some memory of hyperbolic cache"
				"errno=%d (%s)\n",
				rte_errno, rte_strerror(rte_errno));*/
	}

	if (!is_prng_set) {
		//rte_srand(time(NULL));
		is_prng_set = true;
	}

	hc->root = NULL;
	hc->sample_size = *(uint32_t *)arg;

	hc->set = calloc(hc->sample_size, sizeof(item *));
	if (!hc->set) {
		/*
		rte_exit(EXIT_FAILURE,
				"Failed to allocate some memory of hyperbolic cache"
				"errno : %d, %s\n",
				rte_errno, rte_strerror(rte_errno));*/
	}

	//rte_spinlock_init(&hc->sl);

	return hc;
}

void 
hyperbolic_cache_insert(void *cache, item *it)
{
	hyperbolic_cache *hc = (hyperbolic_cache *)cache;
	//rte_spinlock_lock(&hc->sl);
	CompleteBinTreeInsert(hc, it);
	//rte_spinlock_unlock(&hc->sl);
}

void
hyperbolic_cache_delete(void *cache, item *it)
{
	hyperbolic_cache *hc = (hyperbolic_cache *)cache;
	//rte_spinlock_lock(&hc->sl);
	CompleteBinTreeDelete(hc, it);
	//rte_spinlock_unlock(&hc->sl);
}

item *
hyperbolic_cache_offloading_candidate(void *cache) 
{
	item *it;
	hyperbolic_cache *hc = (hyperbolic_cache *)cache;
	//rte_spinlock_lock(&hc->sl);
	it = HyperbolicCacheGetOffloadingCandidate(hc);
	//rte_spinlock_unlock(&hc->sl);
	hyperbolic_cache_delete(cache, it);
	return it;
}

item *
hyperbolic_cache_eviction_candidate(void *cache)
{
	item *it;
	hyperbolic_cache *hc = (hyperbolic_cache *)cache;
	//rte_spinlock_lock(&hc->sl);
	it = HyperbolicCacheGetEvictionCandidate(hc);
	//rte_spinlock_unlock(&hc->sl);
	hyperbolic_cache_delete(cache, it);
	return it;
}

void
hyperbolic_cache_access_item(void *cache, item *it) 
{
	uint16_t st;
	//UNUSED(cache);
	//st = rte_atomic16_read(&it->state);
	if (st == ITEM_STATE_AT_NIC) {
		item_incr_refcount(it);
		it->vv->n_requests++;
	} else if (st == ITEM_STATE_AT_HOST) {
		struct timespec cur_ts;
		//GET_CURRENT_TIMESPEC(&cur_ts);
		it->vv->n_requests++;
	} else {
		//log_warning("Item(hv=%lu) is in a wrong state %u\n", it->sv->hv, st);
	}
}

void
hyperbolic_cache_free_item(void *cache, item *it)
{
	uint16_t st;
	//UNUSED(cache);
	//st = rte_atomic16_read(&it->state);
	if (st == ITEM_STATE_AT_NIC) {
		item_decr_refcount(it);
	}
}

void 
hyperbolic_cache_destroy(void *cache)
{
	hyperbolic_cache *hc = (hyperbolic_cache *)cache;
	free(hc->set);
	free(cache);
}
