#include "chnk_seq_tbl.h"
#include "core.h"

struct chnk_seq_tbl_s {
//	uint64_t hv;
	size_t mask;
	size_t nb_entries;
	chnk_seq **tbl;
	size_t nb_tot_chnk_seq;
	size_t nb_free_chnk_seq;
	ssize_t nb_chnk;
	chnk_seq *free_chnk_seq_base;
	chnk_seq *free_chnk_seq;
};

struct chnk_seq_s {
	uint16_t c_seq;
	uint16_t c_sz;
	off_t f_off;
	chnk_seq *c_next;
	chnk_seq *c_prev;
};

chnk_seq_tbl *
chnk_seq_tbl_create(size_t max_nb_eles) {

	chnk_seq_tbl *cst;
	int i;

	cst = calloc(1, sizeof(chnk_seq_tbl));
	if (!cst) {
		perror("Fail to allocate memory for chnk_seq_tbl");
		exit(EXIT_FAILURE);
	}

	cst->mask = max_nb_eles - 1;
	cst->nb_entries = max_nb_eles;
	cst->nb_tot_chnk_seq = max_nb_eles;
	cst->nb_free_chnk_seq = max_nb_eles;

	cst->free_chnk_seq_base = calloc(max_nb_eles, sizeof(chnk_seq));
	if (!cst->free_chnk_seq_base) {
		perror("Fail to allocate memory for free_chn_seq_base");
		exit(EXIT_FAILURE);
	}

	cst->free_chnk_seq = cst->free_chnk_seq_base;

	for (i = 0; i < max_nb_eles - 1; i++) {
		cst->free_chnk_seq[i].c_next = &cst->free_chnk_seq[i+1];
	}

	cst->tbl = calloc(max_nb_eles, sizeof(chnk_seq *));
	if (!cst->tbl) {
		perror("Fail to allocate memory for table");
		exit(EXIT_FAILURE);
	}

	return cst;
}

void
chnk_seq_tbl_insert(chnk_seq_tbl *cst, uint16_t c_seq, uint16_t c_sz, off_t f_off) {

	chnk_seq *cs;
	size_t entry;

	if (unlikely(!cst->free_chnk_seq)) {
		/* Never happen */
		perror("No free chunk_seq in free pool!!!");
		exit(EXIT_FAILURE);
	}

	cs = cst->free_chnk_seq;
	cst->free_chnk_seq = cs->c_next;
	cst->nb_free_chnk_seq--;

	cs->c_seq = c_seq;

	entry = c_seq & cst->mask;

	if (!cst->tbl[entry]) {
		cst->tbl[entry] = cs;
		cs->c_next = NULL;
		cs->c_prev = NULL;
	} else {
		cs->c_next = cst->tbl[entry];
		cs->c_prev = NULL;
		cst->tbl[entry]->c_prev = NULL;
		cst->tbl[entry] = cs;
	}

	cst->nb_chnk++;

	assert(cst->nb_chnk > 0);
}

int
chnk_seq_tbl_delete(chnk_seq_tbl *cst, uint16_t c_seq)
{
	size_t entry;
	chnk_seq *cs = NULL;

	entry = c_seq & cst->mask;
	cs = cst->tbl[entry];

	for (; cs; cs = cs->c_next) {
		if (cs->c_seq == c_seq)
			break;
	}

	if (!cs) {
		/* already deleted */
		return -1;
	}

	if (cs == cst->tbl[entry]) {
		cst->tbl[entry] = cst->tbl[entry]->c_next;
		if (cst->tbl[entry])
			cst->tbl[entry]->c_prev = NULL;
	} else {
		cs->c_prev->c_next = cs->c_next;
		if (cs->c_next)
			cs->c_next->c_prev = cs->c_prev;
	}

	cs->c_prev = NULL;
	cs->c_next = cst->free_chnk_seq->c_next;
	cst->free_chnk_seq = cs;
	cst->nb_free_chnk_seq++;
	cst->nb_chnk--;

	assert(cst->nb_chnk >= 0);

	return 0;
}

void
chnk_seq_tbl_destroy(chnk_seq_tbl *cst)
{
	free(cst->free_chnk_seq_base);
	free(cst);
}

bool
chnk_seq_tbl_empty(chnk_seq_tbl *cst) {
	return cst->nb_chnk == 0;
}

extern void 
GenerateOffloadPacket(optim_cache_context *oc_ctx, item *oc,
		int f_fd, uint16_t f_sz, off_t f_off, uint16_t seq);

void 
chnk_seq_tbl_send_una_chunk(optim_cache_context *oc_ctx, item *oc, int f_fd) {

	int i;
	chnk_seq_tbl *cst = oc_ctx->oc_wait;
	chnk_seq *walk;
	for (i = 0; i < cst->nb_entries; i++) {
		for (walk = cst->tbl[i]; walk; walk = walk->c_next)
			GenerateOffloadPacket(oc_ctx, oc, f_fd, walk->c_sz, walk->f_off, walk->c_seq);
	}
}


#if 0
#define NB_CHNK 100

static void
print_c_seq(void *opaque, uint16_t c_seq)
{
	(void)opaque;

	printf("%u ", c_seq);
}

int
main(void) {
	uint16_t c_seq[NB_CHNK];
	chnk_seq_tbl *cst = chnk_seq_tbl_create(512);

	for (int i = 0; i < NB_CHNK; i++)  {
		c_seq[i] = i;
		chnk_seq_tbl_insert(cst, c_seq[i]);
	}

	for (int i = 0; i < NB_CHNK / 2; i++) {
		chnk_seq_tbl_delete(cst, c_seq[i]);
	}

	for (int i = 0; i < NB_CHNK / 2; i++) {
		chnk_seq_tbl_insert(cst, c_seq[i]);
	}

	chnk_seq_tbl_traversal(cst, print_c_seq, NULL);


	chnk_seq_tbl_destroy(cst);
}
#endif
