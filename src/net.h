#ifndef __NET_H__
#define __NET_H__

#include "core.h"
#include "item.h"

#define NB_TX_THRESHOLD 8

void net_send_offloading_message(optim_cache_context *oc_ctx, item *oc);
void net_send_eviction_message(optim_cache_context *oc_ctx);
void net_flush_tx_pkts(optim_cache_context *oc_ctx, uint16_t portid, uint16_t qid);

#endif
