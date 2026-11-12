#ifndef __GROOVE_STORE_BRIDGE_H__
#define __GROOVE_STORE_BRIDGE_H__

#include <stdint.h>

typedef enum {
  GR_STORE_LOADING, GR_STORE_READY, GR_STORE_UNAVAILABLE, GR_STORE_BUYING,
  GR_STORE_PENDING, GR_STORE_PURCHASED, GR_STORE_PURCHASE_FAILED,
  GR_STORE_RESTORING, GR_STORE_RESTORED, GR_STORE_RESTORE_FAILED
} groove_store_status_t;

const char *groove_store_product_id(int32_t product);
void groove_store_set_price(int32_t product, const char *price);
void groove_store_set_entitlements(uint32_t owned);
void groove_store_set_status(groove_store_status_t status);
void groove_store_purchase_resolved(void);

void groove_storekit_start(void);
void groove_storekit_load(void);
void groove_storekit_buy(int32_t product);
void groove_storekit_restore(void);

#endif
