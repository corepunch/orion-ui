#ifndef __GROOVE_STORE_H__
#define __GROOVE_STORE_H__

#include "store_bridge.h"

typedef enum {
  GR_PACK_FREE = -1, GR_PACK_FULL, GR_PACK_DRUMS, GR_PACK_BASS, GR_PACK_GUITAR,
  GR_PACK_SYNTHS, GR_PACK_VOCALS, GR_PACK_FX, GR_PACK_COUNT
} groove_pack_t;

groove_pack_t groove_store_block_pack(int block);
bool groove_store_allows(int block, uint32_t owned);

#if GROOVE_APP_STORE
bool groove_store_can_add(int block);
void groove_store_show(void);
#else
static inline bool groove_store_can_add(int block) { (void)block; return true; }
#endif

#endif
