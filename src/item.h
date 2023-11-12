#ifndef __ITEM_H__
#define __ITEM_H__

#include <stdint.h>
#include <stddef.h>
#include <sys/queue.h>
#include <rte_atomic.h>
#include <stdbool.h>

#define ITEM_STATE_AT_NOWHERE	                        (uint8_t)0
#define ITEM_STATE_AT_HOST		                        (uint8_t)1
#define ITEM_STATE_AT_NIC		                        (uint8_t)2
#define ITEM_STATE_AT_WAIT_QUEUE						(uint8_t)3
#define ITEM_STATE_AT_DELETE_QUEUE                      (uint8_t)4
#define ITEM_STATE_WAITING_FOR_OFFLOADING				(uint8_t)5
#define ITEM_STATE_WAITING_FOR_EVICTION                 (uint8_t)6

typedef struct varying_value_s varying_value;
typedef struct static_value_s static_value;
typedef struct item_s item;

typedef struct static_value_s static_value;

struct varying_value_s {
	uint32_t n_requests;
	uint32_t ts_ms;
	rte_atomic32_t refcount;
	double prio;
};

struct static_value_s {
	uint64_t hv;
	uint32_t sz;
};

struct item_s {
	rte_atomic16_t state;
	varying_value *vv;
	static_value *sv;

	/* For Set or LFU */
	item *left;
	item *right;
	item *parent;
	uint8_t color : 1,
			reserved : 7;
#if (HOST_LRU || NIC_LRU)
	TAILQ_ENTRY(item_s) lruqLink;
#endif
	char *key;        /* file path */
	size_t keylen;    /* file path length */

	void *bucket;    /* Hashtable bucket that the item is included */
	void *ctx;

	TAILQ_ENTRY(item_s) hashLink;
	TAILQ_ENTRY(item_s) queueLink;
};

int item_set_state(item *it, uint16_t desired);
void item_incr_refcount(item *it);
void item_decr_refcount(item *it);
uint64_t item_get_hv(item *it);
uint64_t item_get_nb_requests(item *it);
bool item_cmp_hv(item *x, item *y);

#define item_get_refcount(_it) rte_atomic32_read(&_it->vv->refcount)

#endif
