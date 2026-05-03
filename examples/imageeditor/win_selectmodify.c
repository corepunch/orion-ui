// Selection Modify dialog
// Reusable modal dialog for Select > Expand and Select > Contract.

#include "imageeditor.h"

#define SM_MIN_AMOUNT 1
#define SM_MAX_AMOUNT 1024

typedef struct { int amount; } sm_state_t;

static const ctrl_binding_t k_sm_bindings[] = {
  DDX_TEXT(SM_ID_AMOUNT, sm_state_t, amount),
};

bool show_selection_modify_dialog(window_t *parent, const char *title, int *out_amount) {
  sm_state_t st = { .amount = *out_amount };
  form_def_t form = imageeditor_selection_modify_form;
  form.bindings = k_sm_bindings;
  form.binding_count = ARRAY_LEN(k_sm_bindings);
  form.ok_id = SM_ID_OK;
  form.cancel_id = SM_ID_CANCEL;
  if (!show_ddx_dialog(&form, title, parent, &st)) return false;
  if (st.amount >= SM_MIN_AMOUNT && st.amount <= SM_MAX_AMOUNT)
    *out_amount = st.amount;
  return true;
}
