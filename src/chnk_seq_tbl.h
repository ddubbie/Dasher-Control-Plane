#ifndef __CHNK_SEQ_HT_H__
#define __CHNK_SEQ_HT_H__

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct chnk_seq_tbl_s chnk_seq_tbl;
typedef struct chnk_seq_s chnk_seq;

chnk_seq_tbl *chnk_seq_tbl_create(size_t max_nb_eles);
void chnk_seq_tbl_insert(chnk_seq_tbl *cst, uint16_t c_seq, uint16_t c_sz, off_t f_off);
int chnk_seq_tbl_delete(chnk_seq_tbl *cst, uint16_t c_seq);
void chnk_seq_tbl_destroy(chnk_seq_tbl *cst);
bool chnk_seq_tbl_empty(chnk_seq_tbl *cst);

#include "core.h"

void chnk_seq_tbl_send_una_chunk(optim_cache_context *oc_ctx, item *oc, int f_fd);
#endif
