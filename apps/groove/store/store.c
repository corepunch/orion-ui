#include "groove.h"
#include "store.h"

static const char *const kGrooveStarterBank[] = {
#include "starter_bank.h"
};

static int groove_starter_key_compare(const void *key, const void *entry) {
  return strcmp(key, *(const char *const *)entry);
}

groove_pack_t groove_store_block_pack(int block) {
  if (block < 0 || block >= blocks_builtin_count()) return GR_PACK_FREE;
  const block_t *b = block_get(block);
  if (bsearch(b->name, kGrooveStarterBank, ARRAY_LEN(kGrooveStarterBank),
              sizeof(kGrooveStarterBank[0]), groove_starter_key_compare)) return GR_PACK_FREE;
  static const groove_pack_t packs[FAMILY_COUNT] = {
    [FAMILY_LOOP] = GR_PACK_DRUMS, [FAMILY_DRUM] = GR_PACK_DRUMS,
    [FAMILY_BASS] = GR_PACK_BASS,  [FAMILY_GUITAR] = GR_PACK_GUITAR,
    [FAMILY_SYNTH] = GR_PACK_SYNTHS, [FAMILY_PAD] = GR_PACK_SYNTHS, [FAMILY_STAB] = GR_PACK_SYNTHS,
    [FAMILY_VOX] = GR_PACK_VOCALS, [FAMILY_SCRATCH] = GR_PACK_FX, [FAMILY_FX] = GR_PACK_FX,
    [FAMILY_SAMPLES] = GR_PACK_FREE
  };
  return packs[b->family];
}

bool groove_store_allows(int block, uint32_t owned) {
  groove_pack_t pack = groove_store_block_pack(block);
  return pack == GR_PACK_FREE || (owned & ((1u << GR_PACK_FULL) | (1u << pack)));
}

static const char *const kGrooveProducts[GR_PACK_COUNT] = {
  [GR_PACK_FULL]   = "com.orion.groove.full",
  [GR_PACK_DRUMS]  = "com.orion.groove.drums",
  [GR_PACK_BASS]   = "com.orion.groove.bass",
  [GR_PACK_GUITAR] = "com.orion.groove.guitar",
  [GR_PACK_SYNTHS] = "com.orion.groove.synths",
  [GR_PACK_VOCALS] = "com.orion.groove.vocals",
  [GR_PACK_FX]     = "com.orion.groove.fx",
};

static window_t *groove_store_window;
static uint32_t groove_owned_packs;
static groove_store_status_t groove_store_status = GR_STORE_LOADING;
static char groove_prices[GR_PACK_COUNT][48];

const char *groove_store_product_id(int32_t product) {
  return product >= 0 && product < GR_PACK_COUNT ? kGrooveProducts[product] : NULL;
}

void groove_store_set_price(int32_t product, const char *price) {
  if (!groove_store_product_id(product)) {
    fprintf(stderr, "[groove-store] price update rejected product=%d\n", product);
    fflush(stderr);
    return;
  }
  snprintf(groove_prices[product], sizeof(groove_prices[product]), "%s", price ? price : "");
}

void groove_store_set_entitlements(uint32_t owned) {
  if (owned == groove_owned_packs) return;
  groove_owned_packs = owned;
  if (g_app && g_app->bin) invalidate_window(g_app->bin);
  if (groove_store_window) post_message(groove_store_window, grStoreChanged, 0, NULL);
}

void groove_store_set_status(groove_store_status_t status) {
  groove_store_status = status;
  if (groove_store_window) post_message(groove_store_window, grStoreChanged, 0, NULL);
}

void groove_store_purchase_resolved(void) {
  if (groove_store_status == GR_STORE_PENDING) groove_store_set_status(GR_STORE_PURCHASED);
}

#if GROOVE_APP_STORE

static const uint16_t kGroovePackButtons[GR_PACK_COUNT] = {
  ID_SAMPLE_STORE_FULL, ID_SAMPLE_STORE_DRUMS, ID_SAMPLE_STORE_BASS, ID_SAMPLE_STORE_GUITAR,
  ID_SAMPLE_STORE_SYNTHS, ID_SAMPLE_STORE_VOCALS, ID_SAMPLE_STORE_FX
};

bool groove_store_can_add(int block) {
  return groove_store_allows(block, groove_owned_packs);
}

static bool groove_store_busy(void) {
  return groove_store_status == GR_STORE_LOADING || groove_store_status == GR_STORE_BUYING ||
         groove_store_status == GR_STORE_RESTORING;
}

static bool groove_pack_owned(int product) {
  const uint32_t packs = ((1u << GR_PACK_COUNT) - 1) & ~(1u << GR_PACK_FULL);
  return (groove_owned_packs & ((1u << GR_PACK_FULL) | (1u << product))) ||
         (product == GR_PACK_FULL && (groove_owned_packs & packs) == packs);
}

static const char *groove_store_status_text(void) {
  switch (groove_store_status) {
    case GR_STORE_LOADING:         return ui_tr("Loading prices from the App Store…");
    case GR_STORE_READY:           return ui_tr("Each purchase is a one-time payment.");
    case GR_STORE_UNAVAILABLE:     return ui_tr("Some prices are unavailable. Reopen Sample Packs to retry.");
    case GR_STORE_BUYING:          return ui_tr("Waiting for the App Store…");
    case GR_STORE_PENDING:         return ui_tr("Purchase pending. The pack unlocks when Apple approves it.");
    case GR_STORE_PURCHASED:       return ui_tr("Purchase complete. Your new sounds are ready to use.");
    case GR_STORE_PURCHASE_FAILED: return ui_tr("The purchase could not be completed. Please try again.");
    case GR_STORE_RESTORING:       return ui_tr("Restoring your purchases…");
    case GR_STORE_RESTORED:        return groove_owned_packs ? ui_tr("Your purchases are up to date.") : ui_tr("No previous purchases were found.");
    case GR_STORE_RESTORE_FAILED:  return ui_tr("Purchases could not be restored. Please try again.");
    default:                      return "";
  }
}

static void groove_store_refresh(window_t *win) {
  for (int i = 0; i < GR_PACK_COUNT; i++) {
    window_t *button = get_window_item(win, kGroovePackButtons[i]);
    bool owned = groove_pack_owned(i);
    const char *price = groove_prices[i];
    char caption[64];
    if (owned) snprintf(caption, sizeof(caption), "%s", ui_tr("Unlocked"));
    else if (*price) snprintf(caption, sizeof(caption), ui_tr("Buy for %s"), price);
    else snprintf(caption, sizeof(caption), "%s", ui_tr("Unavailable"));
    set_window_title(button, caption);
    enable_window(button, !owned && *price && !groove_store_busy());
  }
  set_window_title(get_window_item(win, ID_SAMPLE_STORE_STATUS), groove_store_status_text());
  enable_window(get_window_item(win, ID_SAMPLE_STORE_RESTORE), !groove_store_busy());
  window_layout_sync(win);
}

static result_t groove_store_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: groove_store_refresh(win); return true;
    case evPaint:  return false;
    case evClose:  destroy_window(win); return true;
    case evDestroy: groove_store_window = NULL; return true;
    case grStoreChanged: groove_store_refresh(win); return true;
    case evCommand:
      if (HIWORD(wparam) != btnClicked) return false;
      if (LOWORD(wparam) == ID_SAMPLE_STORE_CLOSE) { destroy_window(win); return true; }
      if (groove_store_busy()) return true;
      if (LOWORD(wparam) == ID_SAMPLE_STORE_RESTORE) {
        groove_store_set_status(GR_STORE_RESTORING);
        groove_storekit_restore();
        return true;
      }
      for (int i = 0; i < GR_PACK_COUNT; i++) if (LOWORD(wparam) == kGroovePackButtons[i]) {
        if (!groove_pack_owned(i) && groove_prices[i][0]) {
          groove_store_set_status(GR_STORE_BUYING);
          groove_storekit_buy(i);
        }
        return true;
      }
      return false;
    default: return false;
  }
}

void groove_store_show(void) {
  if (groove_store_window) {
    show_window(groove_store_window, true);
    set_focus(groove_store_window);
    return;
  }
  window_t *win = create_window_from_form(&groove_sample_store_form, 0, 0, NULL, groove_store_proc, g_app->hinstance, NULL);
  if (!win) {
    fprintf(stderr, "[groove-store] purchase form creation failed\n");
    fflush(stderr);
    return;
  }
  groove_store_window = win;
  window_set_owner(win, g_app->win);
  layout_measure_t measure = { .avail_w = get_client_rect(win).w };
  send_message(win, evMeasure, 0, &measure);
  irect16_t measured = R(0, 0, measure.avail_w, measure.desired_h);
  adjust_window_rect(&measured, win->flags);
  resize_window(win, win->frame.w, measured.h);
  irect16_t center = center_window_rect(win->frame, g_app->win);
  move_window(win, center.x, center.y);
  show_window(win, true);
  if (!groove_store_busy()) for (int i = 0; i < GR_PACK_COUNT; i++) {
    if (!groove_prices[i][0] && !groove_pack_owned(i)) {
      groove_store_set_status(GR_STORE_LOADING);
      groove_storekit_load();
      break;
    }
  }
}

#endif
