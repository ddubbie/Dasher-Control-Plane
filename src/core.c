#include "control_plane.h"
#include "core.h"
#include "config.h"
#include "item.h"
#include "item_mp.h"
#include "cache.h"
#include "hashtable.h"
#include "debug.h"
#include "net.h"
#include "chnk_seq_tbl.h"
#include "control_plane.h"
#include "item_queue.h"

/* static value */
static private_context *g_private_context[MAX_NB_CPUS] = {NULL};
static optim_cache_context *g_oc_ctx[MAX_NB_CPUS] = {NULL};
static rte_atomic32_t nb_tot_requests;
static cache_method *cm_offloaded = NULL;
static cache_method *cm_unoffloaded = NULL;
static void *offloaded = NULL;
static void *unoffloaded = NULL;
static rte_atomic32_t g_nb_optim_cache_tasks;
static bool run_optim_cache = true;
static pthread_mutex_t mutex_optim_cache = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_optim_cache = PTHREAD_COND_INITIALIZER;
static bool is_ready = false;
static bool mtcp_master_thread_ready = false;

#if OFFLOADING_EVICTION_TEST
/* Test whether the offloading & eviction work well or not */
static void *OffloadingEvictionTestThread(void *opaque);
#define RUN_OFFLOADING_EVICTION_TEST(_test_it) do {\
	int _ret;\
	pthread_t _test_thread;\
	_ret = pthread_create(&_test_thread, NULL, OffloadingEvictionTestThread, (_test_it));\
	if (_ret < 0) {\
		LOG_ERROR("Fail to create OffloadingEvictionTestThread\n");\
		exit(EXIT_FAILURE);\
	}\
	pthread_join(_test_thread, NULL);\
	LOG_INFO("Complete Offloading-Eviction Test\n");\
} while(0)
#else
#define RUN_OFFLOADING_EVICTION_TEST(_test_it) UNUSED(_test_it)
#endif

const static char *o_suffix[] = {
	".html",
	".m4s",
};

/* static function */
static bool CheckSuffix(const char filename[]);
static void HeatDataplane(void);
static optim_cache_context *CreateOptimCachePrivateContext(uint16_t core_index);
static void DestroyOptimCachePrivateContext(optim_cache_context *oc_ctx);
static void EvictItems(optim_cache_context *oc_ctx);
static void OffloadItems(optim_cache_context *oc_ctx);
static int OptimCache(void *opaque);
static void SignalToOptimCache(void);

#define CONTROL_PLANE_CONFIG_PATH "config/control_plane.cfg"
#define MASTER_CORE_INDEX 0

static bool
CheckSuffix(const char filename[]) {
	int nb_o_suffix, i, ret;
	char *suffix;
	nb_o_suffix = sizeof(o_suffix) / sizeof(char *);

	suffix = strchr(filename, '.');
	if (!suffix)
		return false;

	for (i = 0; i < nb_o_suffix; i++) {
		ret = strncmp(suffix, o_suffix[i], sizeof(o_suffix[i]) - 1);
		if (ret == 0)
			return true;
	}

	return false;
}

#if OFFLOADING_EVICTION_TEST
static void *
OffloadingEvictionTestThread(void *opaque) {
	uint32_t nb_blks_before_eviction, nb_blks_after_eviction;
	uint32_t count = 0;
	struct optim_cache_context *oc_ctx = g_oc_ctx[MASTER_CORE_INDEX];
	item *test_it = (item *)opaque;

	TEST_BACKOFF_SLEEP();

	LOG_INFO("Run Offloading-Eviction Test...");

	while (count < MAX_TEST_COUNT) {
		nb_blks_before_eviction = cache_ctrl_get_free_blocks();
		fprintf(stderr, "[Before Eviction] \n"
				"Total Number of Free Blocks = %u\n "
				"Required Number of Offloading Candidate = %u\n "
				"Timestamp(ms) = %u\n "
				"# of reqs = %u\n",
				nb_blks_before_eviction,
				GET_NB_BLK(test_it->sv->sz),
				test_it->vv->ts_ms,
				test_it->vv->n_requests);


		cm_offloaded->delete(offloaded, test_it);
		rb_tree_insert(oc_ctx->ec_wait, test_it);

		EvictItems(oc_ctx);
		TEST_BACKOFF_SLEEP();

		item_enqueue(oc_ctx->ocq, test_it);
		OffloadItems(oc_ctx);

		nb_blks_after_eviction = cache_ctrl_get_free_blocks();

		fprintf(stderr, "[After Eviction] \n"
				"Total Number of Free Blocks = %u\n "
				"Required Number of Offloading Candidate = %u\n "
				"Timestamp(ms) = %u\n "
				"# of reqs = %u\n",
				nb_blks_after_eviction,
				GET_NB_BLK(test_it->sv->sz),
				test_it->vv->ts_ms,
				test_it->vv->n_requests);
		/* Check DPU memory consistency at host */
		assert(nb_blks_before_eviction == nb_blks_after_eviction);
		count++;
	}

	return NULL;
}
#endif

static void
HeatDataplane(void) {
	int i, ret;
	DIR *dir;
	struct dirent *ent;
	char path_buf[512];
	uint32_t nb_required_blks;
	optim_cache_context *oc_ctx;

	for (i = 0; i < (int)CP_CONFIG.nb_dir_path; i++) {
		dir = opendir(CP_CONFIG.dir_path[i]);

		if (!dir) {
			LOG_ERROR("Fail to open directory(%s)\n", CP_CONFIG.dir_path[i]);
			exit(EXIT_FAILURE);
		}

		while ((ent = readdir(dir))) {
			item *ret_it;
			int core_index, path_len;
			private_context *ctx;
			if (!CheckSuffix(ent->d_name))
				continue;

			path_len = sprintf(path_buf, "%s/%s", CP_CONFIG.dir_path[i], ent->d_name);
			if (path_len < 0) {
				perror("Fail to generate absolute path for object");
				exit(EXIT_FAILURE);
			}

			core_index = CAL_HV(path_buf, path_len) % CP_CONFIG.ncpus;
			ctx = g_private_context[core_index];

			ret = hashtable_put(ctx, path_buf, path_len, &ret_it);
			if (ret < 0) {
				LOG_ERROR("Fail to insert object at hashtable\n");
				exit(EXIT_FAILURE);
			}
			oc_ctx = g_oc_ctx[core_index];
			nb_required_blks = GET_NB_BLK(ret_it->sv->sz);

			if (nb_required_blks < cache_ctrl_get_free_blocks()) {
				item_enqueue(oc_ctx->ocq, ret_it);
			} else {
				item_set_state(ret_it, ITEM_STATE_AT_HOST);
				cm_unoffloaded->insert(unoffloaded, ret_it);
			}

			OffloadItems(oc_ctx);
			RUN_OFFLOADING_EVICTION_TEST(ret_it);
		}
		closedir(dir);
	}
}

static optim_cache_context *
CreateOptimCachePrivateContext(uint16_t core_index) {

	optim_cache_context *oc_ctx;

	oc_ctx = malloc(sizeof(optim_cache_context));
	if (!oc_ctx) {
		perror("Fail to allocate memory for optim_cache_context");
		return NULL;
	}

	oc_ctx->ec_wait = rb_tree_create(item_cmp_hv, item_get_hv);
	if (!oc_ctx->ec_wait) 
		return NULL;

	oc_ctx->oc_wait = chnk_seq_tbl_create(PKT_QUEUE_SIZE);
	if (!oc_ctx->oc_wait)
		return NULL;

	oc_ctx->pktmbuf_pool = rte_pktmbuf_pool_create("Offload-pktmbuf-pool",
			NB_MBUFS, RTE_CACHE_LINE_SIZE, 0, PKTMBUF_SIZE, rte_socket_id());
	if (!oc_ctx->pktmbuf_pool) {
		LOG_ERROR("Fail to create pktmbuf_pool for offload\n");
		return NULL;
	}

	oc_ctx->ocq = item_queue_create();
	if (!oc_ctx->ocq)
		return NULL;

	oc_ctx->orq = malloc(sizeof(offload_reply_queue));
	if (!oc_ctx->orq) {
		return NULL;
	}
	pthread_mutex_init(&oc_ctx->orq->mutex, NULL);
	pthread_cond_init(&oc_ctx->orq->cond, NULL);
	oc_ctx->orq->len = 0;
	oc_ctx->orq->proc = false;

	oc_ctx->erq = malloc(sizeof(eviction_reply_queue));
	if (!oc_ctx->erq) {
		return NULL;
	}
	pthread_mutex_init(&oc_ctx->erq->mutex, NULL);
	pthread_cond_init(&oc_ctx->erq->cond, NULL);
	oc_ctx->erq->len = 0;
	oc_ctx->erq->proc = false;

	oc_ctx->rehv = malloc(sizeof(struct rx_e_hv));
	if (!oc_ctx->rehv)
		return NULL;

	oc_ctx->tpq = malloc(sizeof(struct tx_pkt_queue));
	if (!oc_ctx->tpq) 
		return NULL;
	//rte_spinlock_init(&oc_ctx->tpq->sl);
	oc_ctx->tpq->len = 0;
	pthread_mutex_init(&oc_ctx->tpq->mutex, NULL);
	//pthread_cond_init(&oc_ctx->tpq->cond, NULL);

	oc_ctx->core_index = core_index;

	return oc_ctx;
}

static void
DestroyOptimCachePrivateContext(optim_cache_context *oc_ctx) {
	/* TODO */

	pthread_mutex_destroy(&oc_ctx->tpq->mutex);
	free(oc_ctx->tpq);

	free(oc_ctx->rehv);

	pthread_cond_destroy(&oc_ctx->erq->cond);
	pthread_mutex_destroy(&oc_ctx->erq->mutex);
	free(oc_ctx->erq);

	pthread_cond_destroy(&oc_ctx->orq->cond);
	pthread_mutex_destroy(&oc_ctx->orq->mutex);
	free(oc_ctx->orq);

	rte_mempool_free(oc_ctx->pktmbuf_pool);
	chnk_seq_tbl_destroy(oc_ctx->oc_wait);

	rb_tree_destroy(oc_ctx->ec_wait);

	free(oc_ctx);
}

inline static void 
EvictItems(optim_cache_context *oc_ctx) {
/*
	while(!rb_tree_is_empty(oc_ctx->oc_wait)) 
		rb_tree_inorder_traversal(oc_ctx->oc_wait, );*/
	int i;

	if (rb_tree_is_empty(oc_ctx->ec_wait))
		return;

	net_send_eviction_message(oc_ctx);

	for (i = 0; i < oc_ctx->erq->len; i++) {
		uint16_t state;
		item *it; 
		it = hashtable_get_with_hv(NULL, oc_ctx->erq->e_hv[i], &state);
		assert(it);
		UNUSED(state);
		item_set_state(it, ITEM_STATE_AT_HOST);
		cm_unoffloaded->insert(unoffloaded, it);

		cache_ctrl_free_blocks(it);
		assert(cache_ctrl_get_free_blocks() >= GET_NB_BLK(it->sv->sz));
	}
	oc_ctx->erq->len = 0;
}

inline static void
OffloadItems(optim_cache_context *oc_ctx) {

	item *oc;

	while ((oc = item_dequeue(oc_ctx->ocq))) {
		int ret;
		net_send_offloading_message(oc_ctx, oc);

		item_set_state(oc, ITEM_STATE_AT_NIC);
		cm_offloaded->insert(offloaded, oc);

		ret = cache_ctrl_consume_blocks(oc);
		assert(ret >= 0);
	}
}

static int
OptimCache(void *opaque) {

	uint16_t core_index = *(uint16_t *)opaque;
	optim_cache_context *oc_ctx;

	oc_ctx = CreateOptimCachePrivateContext(core_index);
	if (!oc_ctx) {
		perror("Fail to create optim_cache_context");
		exit(EXIT_FAILURE);
	}

	LOG_INFO("Completes to create OptimCache private context\n");

	g_oc_ctx[core_index] = oc_ctx;

	is_ready = true;

	while (run_optim_cache) {
		uint32_t nb_optim_cache_tasks;
		int nb_ocs = 0;
		size_t tot_oc_sz = 0;
		int nb_blk_required;
		item *oc, *ec;
		pthread_mutex_lock(&mutex_optim_cache);

		nb_optim_cache_tasks = rte_atomic32_read(&g_nb_optim_cache_tasks);
		if (nb_optim_cache_tasks <= 0)
			pthread_cond_wait(&cond_optim_cache, &mutex_optim_cache);

		while(nb_ocs < CP_CONFIG.nb_offloads) {
			/* Choose Offloading Candidate */
			oc = cm_unoffloaded->get_oc(unoffloaded);
			item_set_state(oc, ITEM_STATE_WAITING_FOR_OFFLOADING);
			item_enqueue(oc_ctx->ocq, oc);
			tot_oc_sz += oc->sv->sz;
			nb_ocs++;
		}

		nb_blk_required = GET_NB_BLK(tot_oc_sz) - cache_ctrl_get_free_blocks();

		while(nb_blk_required > 0) {
			/* Choose eviction candidate */
			int nb_ecs;
			ec = cm_offloaded->get_ec(offloaded);
			nb_ecs = GET_NB_BLK(ec->sv->sz);
			nb_blk_required -= nb_ecs;

			rb_tree_insert(oc_ctx->ec_wait, ec);
		}

		EvictItems(oc_ctx);
		OffloadItems(oc_ctx);

		rte_atomic32_dec(&g_nb_optim_cache_tasks);
		pthread_mutex_unlock(&mutex_optim_cache);
	}

	DestroyOptimCachePrivateContext(oc_ctx);

	return 0;
}

inline static void
SignalToOptimCache(void) {
#if RUN_CACHE_OPTIMIZATION
	rte_atomic32_inc(&g_nb_optim_cache_tasks);
	pthread_cond_signal(&cond_optim_cache);
#endif
}

void 
control_plane_setup(void) {
	/* Global Setup */
	int i;
	static uint16_t g_core_index[MAX_NB_CPUS] = {0};

	config_parse(CONTROL_PLANE_CONFIG_PATH);
	cache_ctrl_setup();
	hashtable_create(CP_CONFIG.hashpower);

	for (i = 0; i < CP_CONFIG.ncpus; i++) {
		g_private_context[i] = malloc(sizeof(private_context));
		if (!g_private_context[i]) {
			perror("Fail to allocate memory for private_context");
			exit(EXIT_FAILURE);
		}
		g_private_context[i]->lcore_id = CP_CONFIG.lcore_id[i];
		g_private_context[i]->core_index = i;
		g_private_context[i]->itmp = item_mp_create(CP_CONFIG.max_nb_items, CP_CONFIG.lcore_id[i]);
	}

	cm_offloaded = cache_create(NULL, HOST_CACHE, &offloaded);
	cm_unoffloaded = cache_create(NULL, NIC_CACHE, &unoffloaded);

	rte_atomic32_init(&nb_tot_requests);

	for (i = 0; i < CP_CONFIG.ncpus; i++) {
		int ret = -1;
		g_core_index[i] = i;
		ret = rte_eal_remote_launch(OptimCache, &g_core_index[i], (unsigned)CP_CONFIG.lcore_id[i]);
		if (ret < 0) {
			rte_exit(EXIT_FAILURE,
					"Fail to launch OptimCache (%s)\n", rte_strerror(rte_errno));
		}
	}

	do {
		usleep(1000);
	} while(!is_ready);

	//HeatDataplane();
}

inline int
control_plane_get_obj_hv(int core_index, char *url, size_t url_len, uint64_t *obj_hv, uint32_t *obj_sz) {

	item *it = NULL;
	private_context *ctx;
	uint16_t state;
	uint32_t nb_cur_requests;

	if (unlikely(!url)) {
		return -1;
	}

	ctx = g_private_context[core_index];
	it = hashtable_get_with_key(ctx, url, url_len, &state);
	if (!it) {
		LOG_ERROR("[NO_OBJECT] Object(url=%s) does not exist\n", url);
		return -1;
	}

	if (state != ITEM_STATE_AT_NIC) {
		LOG_GET_REQUEST("[INAVAILIALBLE] Object(url=%s) is inavailialbe\n", url);
		return -1;
	}

	cm_offloaded->access(offloaded, it);
	nb_cur_requests = rte_atomic32_add_return(&nb_tot_requests, 1);
	if (core_index == MASTER_CORE_INDEX && nb_cur_requests > CP_CONFIG.nb_reqs_thresh) {
		SignalToOptimCache();
		rte_atomic32_sub(&nb_tot_requests, CP_CONFIG.nb_reqs_thresh);
	}

	*obj_hv = it->sv->hv;
	*obj_sz = it->sv->sz;

	LOG_GET_REQUEST("[SUCCESS] Successfully get object(url=%s), obj_hv=%lu\n", url, obj_hv);

	return 0;
}

inline int
control_plane_free_obj_by_hv(int core_index, uint64_t hv) {

	item *it;
	private_context *ctx;
	uint16_t state;
		
	ctx = g_private_context[core_index];

	it = hashtable_get_with_hv(ctx, hv, &state);
	if (unlikely(!it)) {
		LOG_ERROR("[NO_OBJECT] Object(hv=%lu) does not exist\n", hv);
		return -1;
	}

	if (unlikely(state != ITEM_STATE_AT_NIC)) {
		LOG_ERROR("[INAVAILIABLE] Object(hv=%lu) is already evicted\n", hv);
		return -1;
	}

	cm_offloaded->free(offloaded, it);

	LOG_FREE_REQUEST("[SUCCESS] Object(hv=%lu) is successfully freed\n", hv);

	return 0;
}

inline void
control_plane_enqueue_reply(int core_index, void *pktbuf) {

	optim_cache_context *oc_ctx;
	struct rte_ether_hdr *ethh;
	uint16_t ether_type;

	oc_ctx = g_oc_ctx[core_index];

	ethh = (struct rte_ether_hdr *)pktbuf;
	ether_type = rte_be_to_cpu_16(ethh->ether_type);

	if (ether_type == ETYPE_PAYLOAD_OFFLOAD) {
		struct rx_offload_meta_hdr *omh;
		pthread_mutex_lock(&oc_ctx->orq->mutex);
		omh = (struct rx_offload_meta_hdr *)(ethh + 1);
		//rte_spinlock_lock(&oc_ctx->orq->sl);
		oc_ctx->orq->q[oc_ctx->orq->len].hv = omh->hv;
		oc_ctx->orq->q[oc_ctx->orq->len].sz = omh->sz;
		oc_ctx->orq->q[oc_ctx->orq->len].seq = omh->seq;
		oc_ctx->orq->q[oc_ctx->orq->len].off = omh->off;
		oc_ctx->orq->len++;
		//LOG_INFO("Obj(hv=%lu, seq=%u) offloading reply\n", omh->hv, omh->seq);
	/*	uint64_t ms_now;
		GET_CUR_MS(ms_now);
		LOG_INFO("RECV=%lu\n", ms_now);*/
		pthread_mutex_unlock(&oc_ctx->orq->mutex);
		//rte_spinlock_unlock(&oc_ctx->orq->sl);
	} else if (ether_type == ETYPE_EVICTION){
		uint64_t *e_hv;
		pthread_mutex_lock(&oc_ctx->erq->mutex);
		//rte_spinlock_lock(&oc_ctx->erq->sl);
		e_hv = (uint64_t *)(ethh + 1);
		oc_ctx->erq->e_hv[oc_ctx->erq->len] = *e_hv;
		oc_ctx->erq->len++;
		pthread_mutex_unlock(&oc_ctx->erq->mutex);
		//rte_spinlock_lock(&oc_ctx->erq->sl);
	}
}

void 
control_plane_flush_message(int core_index, uint16_t portid, uint16_t qid) {

	optim_cache_context *oc_ctx;
	oc_ctx = g_oc_ctx[core_index];
	//uint64_t ms_prev, ms_now;
//	rte_spinlock_lock(&oc_ctx->tpq->sl);
	net_flush_tx_pkts(oc_ctx, portid, qid);
//	rte_spinlock_unlock(&oc_ctx->tpq->sl);
}

void 
control_plane_teardown(void) {
	/* TODO */
}

inline int
control_plane_get_nb_cpus(void) {
	return CP_CONFIG.ncpus;
}

void
control_plane_heat_dataplane(void) {
	do {
		usleep(1000);
	} while(!mtcp_master_thread_ready);
	HeatDataplane();
}

void 
control_plane_mtcp_master_thread_ready(void) {
	mtcp_master_thread_ready = true;
	LOG_INFO("MTCP Master Thread is Ready\n");
}

inline void
control_plane_signal_to_replyq(int core_index) {

	optim_cache_context *oc_ctx;
	oc_ctx = g_oc_ctx[core_index];

	if (oc_ctx->erq->len > 0 && !oc_ctx->erq->proc) {
		pthread_cond_signal(&oc_ctx->erq->cond);
	} 
	if (oc_ctx->orq->len > 0 && !oc_ctx->orq->proc)  {
		pthread_cond_signal(&oc_ctx->orq->cond);
	}
}
