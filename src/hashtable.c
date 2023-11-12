#include <xxhash.h>
#include <string.h>

#include "hashtable.h"
#include "debug.h"

struct item_ht_s {
	uint32_t n_entry;
	uint32_t mask;
	item_bucket *b;
};

static struct item_ht_s ht;
static rte_spinlock_t *ht_sl;

void
hashtable_create(uint16_t hash_power) 
{
	int i;
	if (hash_power < 15) {
		LOG_ERROR("hash_power must be > 20, hash_power is set to default value, 20\n");
		hash_power = 14;
	}

	ht.n_entry = (1U << hash_power);
	ht.mask = ht.n_entry - 1;

	ht.b = rte_malloc(NULL, sizeof(item_bucket) * ht.n_entry, ht.n_entry);
	if (!ht.b) {
		rte_exit(EXIT_FAILURE, 
				" Fail to allocate some memory for hash bucket, "
				"rte_errno : %d, (%s)\n",
				rte_errno, rte_strerror(rte_errno));
	}

	for (i = 0; i < ht.n_entry; i++)
		TAILQ_INIT(&ht.b[i]);

	ht_sl = rte_malloc(NULL, sizeof(rte_spinlock_t) * ht.n_entry, ht.n_entry);
	if (!ht_sl) {
		rte_exit(EXIT_FAILURE,
				"Fail to allocate some memory for hashtable spinlock, "
				"rte_errno : %d, (%s)\n",
				rte_errno, rte_strerror(rte_errno));
	}

	for (i = 0; i < ht.n_entry; i++)
		rte_spinlock_init(&ht_sl[i]);
}

item *
hashtable_get_with_key(private_context *ctx, void *key, const size_t keylen, uint16_t *st)
{
	item *it;
	item_bucket *b;
	rte_spinlock_t *sl;
	uint32_t bucket_index;
	uint64_t hv;
	UNUSED(ctx);

	//hv = XXH3_64bits(key, keylen);
	hv = CAL_HV(key, keylen);
	bucket_index = hv & ht.mask;
	b = &ht.b[bucket_index];
	sl = &ht_sl[bucket_index];

	rte_spinlock_lock(sl);

	TAILQ_FOREACH(it, b, hashLink)
		if (it->sv->hv == hv)
			break;

	if (!it) {
		rte_spinlock_unlock(sl);
		return NULL;
	}

	if (likely(st))
		*st = rte_atomic16_read(&it->state);

	rte_spinlock_unlock(sl);
	return it;
}


item *
hashtable_get_with_hv(private_context *ctx, uint64_t hv, uint16_t *st)
{
	uint32_t b_idx;
	item_bucket *b;
	rte_spinlock_t *sl;
	item *it;

	UNUSED(ctx);

	b_idx = hv & ht.mask;
	b = &ht.b[b_idx];
	sl = &ht_sl[b_idx];

	rte_spinlock_lock(sl);

	TAILQ_FOREACH(it, b, hashLink) {
		if (it->sv->hv == hv)
			break;
	}

	if (!it) {
		rte_spinlock_unlock(sl);
	}

	if (likely(st))
		*st = rte_atomic16_read(&it->state);
/*
	if (st == ITEM_STATE_AT_DELETE_QUEUE || st == ITEM_STATE_AT_WAIT_QUEUE
			|| st == ITEM_STATE_AT_NOWHERE) {
		rte_spinlock_unlock(sl);
		return NULL;
	}*/

	//item_incr_refcount(it);
	rte_spinlock_unlock(sl);
	return it;
}

void
hashtable_free_item(item *it) 
{
	//item_decr_refcount(it);
}

int 
hashtable_put(private_context *ctx, void *key, const size_t keylen, item **ret_it)
{
	uint32_t bucket_index;
	uint64_t hv;
	struct stat f_stat;
	//struct timespec cur_ts;
	rte_spinlock_t *sl;
	item_bucket *b;
	item *it = NULL;

	//hv = XXH3_64bits(key, keylen);
	hv = CAL_HV(key, keylen);
	bucket_index = hv & ht.mask;
	b = &ht.b[bucket_index];
	sl = &ht_sl[bucket_index];

	rte_spinlock_lock(sl);

	TAILQ_FOREACH(it, b, hashLink) {
		if (it->sv->hv == hv) 
			break;
	}

	if (it) {
		/* Update item */
		/* Never occur */
		uint16_t st = rte_atomic16_read(&it->state);
		if (st == ITEM_STATE_AT_NIC) {
			item_set_state(it, ITEM_STATE_AT_WAIT_QUEUE);
			//item_enqueue(g_context.waitq, it);
			rte_spinlock_unlock(sl);
			*ret_it = NULL;
			return HASHTABLE_PUT_ENQUEUE_TO_WAIT_QUEUE;
		}

		free(it->key);
		it->keylen = keylen;
		it->key = strndup(key, keylen);
		if (!it->key) {
			TAILQ_REMOVE(b, it, hashLink);
			//hashtable_reserve_to_delete(c, it);
			*ret_it = NULL;

			rte_spinlock_unlock(sl);
			return HASHTABLE_PUT_FAIL;
		}

		stat(key, &f_stat);

		it->sv->sz = f_stat.st_size;

		GET_CUR_MS(it->vv->ts_ms);
		it->vv->n_requests = 0;
		rte_atomic32_clear(&it->vv->refcount);
		it->vv->prio = 0;

	} else {
		/* Put new item */
		it = item_mp_get(ctx->itmp);
		if (!it) {
			*ret_it = NULL;
			rte_spinlock_unlock(sl);
			return HASHTABLE_PUT_FAIL;
		}

		it->keylen = keylen;
		it->key = strndup(key, keylen);
		if (!it->key) {
			*ret_it = NULL;
			item_mp_free(ctx->itmp, it);
			return HASHTABLE_PUT_FAIL;
		}

		stat(key, &f_stat);

		it->sv->hv = hv;
		it->sv->sz = f_stat.st_size;

		//GET_CURRENT_TIMESPEC(&cur_ts);
		GET_CUR_MS(it->vv->ts_ms);

		it->bucket = b;
		it->ctx = ctx;

		if (ret_it)
			*ret_it = it;

		TAILQ_INSERT_TAIL(b, it, hashLink);
	}

	rte_spinlock_unlock(sl);

	return HASHTABLE_PUT_SUCCESS;
}

#if 0
void
hashtable_reserve_to_delete(cnic *c, item *it)
{
	rte_spinlock_t *sl = &ht_sl[it->sv->hv & ht.mask];
	UNUSED(c);
	rte_spinlock_lock(sl);
	item_set_state(it, ITEM_STATE_AT_DELETE_QUEUE);
	item_enqueue(g_context.deleteq, it);
	rte_spinlock_lock(sl);
}

void
hashtable_delete(item *it) 
{
	cnic *cn;
	rte_spinlock_t *sl = &ht_sl[it->sv->hv & ht.mask];
	rte_spinlock_lock(sl);
	TAILQ_REMOVE((item_bucket *)it->bucket, it, hashLink);
	rte_spinlock_unlock(sl);
	cn = it->cnic;
	item_mp_free(cn->itmp, it);
}
#endif

void 
hashtable_destroy(void)
{
	int i;
	for (i=0; i < ht.n_entry; i++)
		rte_spinlock_unlock(&ht_sl[i]);
	rte_free(ht_sl);
	rte_free(ht.b);

	LOG_INFO("Complete to destroy hashtable\n");
}
