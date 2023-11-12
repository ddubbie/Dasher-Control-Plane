#include "net.h"
#include "config.h"
#include "chnk_seq_tbl.h"
#include "hashtable.h"

struct eviction_meta_hdr {
	uint64_t e_hv;
} __rte_packed;

struct offload_meta_hdr {
	uint64_t c_hv;
	uint32_t c_sz;
	uint16_t c_seq;
	uint64_t c_off;
	uint8_t data[];
};

/* Use RB Tree */
void
GenerateEvictionPacket(optim_cache_context *oc_ctx, item *ec) {

	struct rte_mbuf *m;
	struct rte_ether_hdr *ethh;
	struct eviction_meta_hdr *emh;

	if (unlikely(!(oc_ctx->tpq->len == PKT_QUEUE_SIZE))) /* This case will never occur */
		return;

	m = rte_pktmbuf_alloc(oc_ctx->pktmbuf_pool);
	if (!m) {
		rte_exit(EXIT_FAILURE,
				"Fail to allocate mbuf, errno=%d (%s)\n",
				rte_errno, rte_strerror(rte_errno));
	}

	m->pkt_len = sizeof(struct rte_ether_hdr) + sizeof(struct eviction_meta_hdr);
	m->data_len = m->pkt_len;
	m->nb_segs = 1;
	m->next = NULL;

	oc_ctx->tpq->mq[oc_ctx->tpq->len] = m;
	oc_ctx->tpq->len++;

	ethh = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
	rte_memcpy(&ethh->d_addr, &CP_CONFIG.dpu_mac_addr, sizeof(struct rte_ether_addr));
	rte_memcpy(&ethh->s_addr, &CP_CONFIG.host_mac_addr, sizeof(struct rte_ether_addr));
	ethh->ether_type = rte_cpu_to_be_16(ETYPE_EVICTION);

	emh = (struct eviction_meta_hdr *)(ethh + 1);
	emh->e_hv = ec->sv->hv;
}

void
GenerateOffloadPacket(optim_cache_context *oc_ctx, item *oc,
		int f_fd, uint16_t f_sz, off_t f_off, uint16_t seq) {
	struct rte_mbuf *m;
	struct rte_ether_hdr *ethh;
	struct offload_meta_hdr *omh;
	ssize_t toRead, n_rd;
	off_t off = 0;

	if (unlikely(!(oc_ctx->tpq->len == PKT_QUEUE_SIZE))) /* Never occurs */
		return;

	m = rte_pktmbuf_alloc(oc_ctx->pktmbuf_pool);
	if (unlikely(!m)) {
		rte_exit(EXIT_FAILURE,
				"Fail to allocate mbuf, errno=%d (%s)\n",
				rte_errno, rte_strerror(rte_errno));
	}

	m->pkt_len = sizeof(struct rte_ether_hdr) + sizeof(struct eviction_meta_hdr) + f_sz;
	m->data_len = m->pkt_len;
	m->nb_segs = 1;
	m->next = NULL;

	oc_ctx->tpq->mq[oc_ctx->tpq->len] = m;
	oc_ctx->tpq->len++;

	ethh = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
	rte_memcpy(&ethh->d_addr, &CP_CONFIG.dpu_mac_addr, sizeof(struct rte_ether_addr));
	rte_memcpy(&ethh->s_addr, &CP_CONFIG.host_mac_addr, sizeof(struct rte_ether_addr));
	ethh->ether_type = rte_cpu_to_be_16(ETYPE_PAYLOAD_OFFLOAD);

	omh = (struct offload_meta_hdr *)(ethh + 1);
	omh->c_hv = oc->sv->hv;
	omh->c_sz = f_sz;
	omh->c_seq = seq;
	omh->c_off = f_off;
	
	toRead = f_sz;

	if (lseek(f_fd, f_off, SEEK_SET) < 0) {
		perror("Fail to set file pointer");
		exit(EXIT_FAILURE);
	}

	while (toRead > 0) {
		n_rd = read(f_fd, omh->data + off, toRead - off);
		off += n_rd;
		toRead -= n_rd;

		if (n_rd < 0) {
			perror("Fail to read");
			exit(EXIT_FAILURE);
		}
	}
}

inline static void
ProcessOffloadReplyQueue(optim_cache_context *oc_ctx, item *oc) {

	int i;

	rte_spinlock_lock(&oc_ctx->orq->sl);
	for  (i = 0; i < oc_ctx->orq->len; i++) {
		if (oc->sv->hv != oc_ctx->orq->q[i].hv)
			continue;
		assert(oc->sv->sz == oc_ctx->orq->q[i].sz);
		chnk_seq_tbl_delete(oc_ctx->oc_wait, oc_ctx->orq->q[i].seq);
	}
	oc_ctx->orq->len = 0;
	rte_spinlock_unlock(&oc_ctx->orq->sl);
}

inline static void
ProcessEvictionReplyQueue(optim_cache_context *oc_ctx) {
	int i;
	rte_spinlock_lock(&oc_ctx->erq->sl);
	for (i = 0; i < oc_ctx->erq->len; i++) {
		uint16_t state;
		item *it = hashtable_get_with_hv(NULL, oc_ctx->erq->e_hv[i], &state);
		UNUSED(state);
		assert(it);
		rb_tree_delete(oc_ctx->ec_wait, it);
		memcpy(&oc_ctx->rehv->e_hv, &oc_ctx->erq->e_hv, sizeof(uint64_t) * oc_ctx->erq->len);
		oc_ctx->rehv->len = oc_ctx->erq->len;
	}
	rte_spinlock_unlock(&oc_ctx->erq->sl);
}

void 
net_send_eviction_message(optim_cache_context *oc_ctx) {

	/* Send all evition message */
	rb_tree_inorder_traversal(oc_ctx->ec_wait, GenerateEvictionPacket, oc_ctx);

	while (true) {

		WAIT_FOR_EVICTION();

		ProcessEvictionReplyQueue(oc_ctx);

		if (!rb_tree_is_empty(oc_ctx->ec_wait))
			break;

		rb_tree_inorder_traversal(oc_ctx->ec_wait, GenerateEvictionPacket, oc_ctx);
	}
}

void
net_send_offloading_message(optim_cache_context *oc_ctx, item *oc) {

	uint16_t seq = 0;
	off_t f_off = 0;
	size_t f_sz;
	int f_fd;
	uint16_t toSend;

	seq = 0;

	do {
		f_fd = open(oc->key, O_RDONLY);
	} while((f_fd < 0) && (errno == EAGAIN));

	if (f_fd < 0 && errno != EAGAIN) {
		perror("Fail to open file for offloading");
		exit(EXIT_FAILURE);
	}

	f_sz = oc->sv->sz;
	f_off = 0;

	/* Send All Chunks */ 
	while (f_sz > 0) {
		toSend = (uint16_t)RTE_MIN(f_sz, DATAPLANE_MAX_OFFLOAD_CHUNK_SIZE);
		rte_spinlock_lock(&oc_ctx->tpq->sl);
		GenerateOffloadPacket(oc_ctx, oc, f_fd, toSend, f_off, seq);
		rte_spinlock_unlock(&oc_ctx->tpq->sl);
		chnk_seq_tbl_insert(oc_ctx->oc_wait, seq, toSend, f_off);
		f_sz -= toSend;
		f_off += toSend;
		seq++;
	}

	while (true) {
		WAIT_FOR_OFFLOADING();
		ProcessOffloadReplyQueue(oc_ctx, oc);
		if (!chnk_seq_tbl_empty(oc_ctx->oc_wait))
			break;
		chnk_seq_tbl_send_una_chunk(oc_ctx, oc, f_fd);
	}

	close(f_fd);
}

void
net_flush_tx_pkts(optim_cache_context *oc_ctx, uint16_t portid, uint16_t qid) {

	uint16_t nb_pkts = oc_ctx->tpq->len;

	rte_spinlock_lock(&oc_ctx->tpq->sl);
	if (nb_pkts > 0) {
		struct rte_mbuf **tx_pkts = oc_ctx->tpq->mq;
		uint16_t ret;

		while (nb_pkts > 0) {
			uint16_t toSend =  RTE_MIN(NB_TX_THRESHOLD, nb_pkts);
			do {
				ret = rte_eth_tx_burst(portid, qid, tx_pkts, toSend);
				tx_pkts += ret;
				toSend -= ret;
				nb_pkts -= ret;
			} while(toSend > 0);
		}
	} 

	oc_ctx->tpq->len = 0;

	rte_spinlock_unlock(&oc_ctx->tpq->sl);
}
