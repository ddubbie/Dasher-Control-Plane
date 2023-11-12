#ifndef __CORE_H__
#define __CORE_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h> 
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdarg.h>
#include <fcntl.h>
#include <dirent.h>
#include <xxhash.h>

#include <sys/queue.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <rte_memory.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>

#include <rte_spinlock.h>
#include <rte_malloc.h>
#include <rte_errno.h>
#include <rte_mempool.h>
#include <rte_common.h>
#include <rte_log.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_cfgfile.h>
#include <rte_memcpy.h>

#include <rte_ether.h>
#include <rte_byteorder.h>
#include <rte_branch_prediction.h>

#include "item_mp.h"
#include "item_queue.h"
#include "rb_tree.h"
#include "chnk_seq_tbl.h"

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#ifndef UNUSED
#define UNUSED(_x)	(void)(_x)
#endif

#define MAX_NB_CPUS 12
#define PKT_QUEUE_SIZE 2048
#define NB_MBUFS 8192
#define PKTMBUF_SIZE 16384

#define DEBUG TRUE
#define OFFLOADING_EVICTION_TEST TRUE
#define RUN_CACHE_OPTIMIZATION FALSE

#define DELETE_ITEM_PROCESS_CYCLE	10
#define WAIM_ITEM_PROCESS_CYCLE     10

#define HOST_HYPERBOLIC      TRUE
#define HOST_LFU	         FALSE
#define HOST_LRU			 FALSE

#ifdef HOST_HYPERBOLIC
#define HYPERBOLIC_SAMPLE_SIZE	    (100)
#define HYPERBOLIC_OMEGA			(0.1)
#define HYPERBOLIC_MAX_LOOP_COUNT	(3)
#endif

#define NIC_HYPERBOLIC		 TRUE
#define NIC_LFU				 FALSE
#define NIC_LRU				 FALSE

#ifdef NIC_HYPERBOLIC
#define NIC_SAMPLE_SIZE		3
#endif

#define ETYPE_PAYLOAD_OFFLOAD   (0xf80d)
#define ETYPE_TRANSMISSION      (0xf80f)
#define ETYPE_EVICTION          (0xf811)

typedef struct private_context private_context;
typedef struct optim_cache_context optim_cache_context;
typedef struct offload_reply_queue offload_reply_queue;
typedef struct eviction_reply_queue eviction_reply_queue;
typedef struct tx_pkt_queue tx_pkt_queue;

struct tx_pkt_queue {
	size_t len;
	rte_spinlock_t sl;
	struct rte_mbuf *mq[PKT_QUEUE_SIZE];
};

struct rx_offload_meta_hdr {
	uint64_t hv;
	uint32_t sz;
	uint16_t seq;
	uint64_t off;
} __rte_packed;

struct offload_reply_queue {
	size_t len;
	rte_spinlock_t sl;
	struct rx_offload_meta_hdr q[PKT_QUEUE_SIZE];
};

struct eviction_reply_queue {
	size_t len;
	rte_spinlock_t sl;
	uint64_t e_hv[PKT_QUEUE_SIZE];
};

struct rx_e_hv {
	uint64_t e_hv[PKT_QUEUE_SIZE];
	size_t len;
};

struct private_context {
	uint16_t core_index;
	uint16_t lcore_id;
	item_mp *itmp;
};

struct optim_cache_context {
	uint16_t core_index;
	rb_tree *ec_wait;
	chnk_seq_tbl *oc_wait;
	struct rte_mempool *pktmbuf_pool;
	tx_pkt_queue *tpq;
	offload_reply_queue *orq;
	eviction_reply_queue *erq;
	struct rx_e_hv *rehv;
	item_queue *ocq;
};

#define DATAPLANE_MAX_OFFLOAD_CHUNK_SIZE (8*1024)
#define GET_NB_BLK(sz) sz % DATAPLANE_BLOCK_SIZE ? \
	(sz / DATAPLANE_BLOCK_SIZE) + 1 : \
	(sz / DATAPLANE_BLOCK_SIZE)

#define DATAPLANE_BLOCK_SIZE (16 * 1024)

#define US_WAIT_FOR_OFFLOADING	2000	// (us)
#define WAIT_FOR_OFFLOADING()  usleep(US_WAIT_FOR_OFFLOADING) // Wait for 2ms
#define US_WAIT_FOR_EVICTION 2000 // us
#define WAIT_FOR_EVICTION() usleep(US_WAIT_FOR_EVICTION) // Wait for 2ms

#define TO_DECIMAL(s) strtol((s), NULL, 10)
#define TO_HEXADECIMAL(s) strtol((s), NULL, 16)
#define TO_REAL_NUMBER(s) strtod((s), NULL)
#define TO_GB(b) (b)*(1024 * 1024 * 1024)

#define SEC_TO_USEC(s) ((s) * 1000000)
#define NSEC_TO_USEC(s) ((ns) / 1000)
#define SEC_TO_MSEC(s) ((s) * 1000)
#define NSEC_TO_MSEC(ns) ((ns) / 1000000)

#define GET_CUR_US(us) do {\
	struct timespec ts_now;\
	clock_gettime(CLOCK_REALTIME, &ts_now);\
	(us) = SEC_TO_USEC(ts_now.tv_sec) + NSEC_TO_USEC(ts_now.tv_nsec);\
} while(0)

#define GET_CUR_MS(ms) do {\
	struct timespec ts_now;\
	clock_gettime(CLOCK_REALTIME, &ts_now);\
	(ms) = SEC_TO_MSEC(ts_now.tv_sec) + NSEC_TO_MSEC(ts_now.tv_nsec);\
} while(0)

#define CAL_HV(k, l) XXH3_64bits((k), (l))

#if OFFLOAD_EVICTION_TEST
#define TEST_US_SLEEP 3	// BackOff time is 3 seconds 
#define TEST_BACKOFF_SLEEP() sleep(TEST_US_SLEEP)
#define TEST_COUNT 3
#endif

#endif
