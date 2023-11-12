#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include "item.h"

inline int
item_set_state(item *it, uint16_t desired)
{
/*
	bool ret;
	uint16_t st;
	uint16_t new = rte_atomic16_read(&it->state);
	uint16_t expected = rte_atomic16_read(&it->state);

	ret = rte_atomic16_cmpset(&it->state, &expected, desired);
	if (ret) {
		return 0;
	}

	st = rte_atomic16_read(&it->state);

	assert(st == ITEM_STATE_AT_DELETE_QUEUE);
	return -1;
	rte_atomic16_set(&it->state, desired);
	*/
	return 0;
}

inline void
item_incr_refcount(item *it) 
{
	/*
	uint32_t refcount;
	rte_atomic32_inc(&it->vv->refcount);
	refcount = rte_atomic32_read(&it->vv->refcount);
	assert(refcount > 0);*/
}

inline void
item_decr_refcount(item *it)
{
	/*
	uint32_t refcount;
	rte_atomic32_dec(&it->vv->refcount);
	refcount = rte_atomic32_read(&it->vv->refcount);
	assert((int)refcount >= 0);*/
}

inline uint64_t
item_get_hv(item *it) {
	return it->sv->hv;
}

inline uint64_t 
item_get_nb_requests(item *it) {
	return (uint64_t)it->vv->n_requests;
}
