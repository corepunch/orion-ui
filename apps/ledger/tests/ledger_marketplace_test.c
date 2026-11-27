#include "test_framework.h"
#include "test_env.h"
#include "apps/ledger/ledger.h"
#include "apps/ledger/marketplace_host.h"
#include "apps/ledger/tests/test_databases.h"
#include <stdatomic.h>
#include <lauxlib.h>
#include <lualib.h>

#define MARKET_SHARE "apps/ledger/share"
static ledger_t market_fixture;
static atomic_uint market_calls;
static atomic_bool market_delay, market_bad;
static const char market_key[]="TEST-KEY-UNIQUE_NOT_FOR_EXPORT";

static bool market_fake(const char *method, const char *url, const char *client, const char *secret, const char *payload,
    const atomic_bool *cancel, char **body, size_t *size, int *status) {
  const char *json=NULL;
  *body=NULL; *size=0; *status=0;
  atomic_fetch_add(&market_calls,1);
  if (!secret || strcmp(secret,market_key)) { atomic_store(&market_bad,true); return false; }
  if (atomic_load(&market_delay)) {
    for (int i=0;i<200 && !atomic_load(cancel);i++) axSleep(5);
    return false;
  }
  if (!strcmp(method,"POST") && client && !strcmp(client,"123") && payload) {
    if (!strcmp(url,"https://api-seller.ozon.ru/v4/posting/fbs/list"))
      json="{\"postings\":[{\"posting_number\":\"ORDER-01\",\"status\":\"awaiting_packaging\",\"products\":["
           "{\"offer_id\":\"0007\",\"sku\":123456,\"quantity\":2}]}],\"has_next\":false}";
    if (!strcmp(url,"https://api-seller.ozon.ru/v3/posting/fbo/list"))
      json="{\"postings\":[],\"has_next\":false}";
    if (!strcmp(url,"https://api-seller.ozon.ru/v4/product/info/stocks"))
      json="{\"total\":1,\"items\":[{\"offer_id\":\"0007\",\"product_id\":123456,\"stocks\":["
           "{\"type\":\"fbs\",\"present\":5,\"reserved\":1}]}]}";
  } else if (!strcmp(method,"GET") && !client && !payload) {
    static const char prefix[]="https://api.sandbox.ebay.com/sell/fulfillment/v1/order?";
    if (!strncmp(url,prefix,sizeof(prefix)-1))
      json="{\"total\":1,\"orders\":[{\"orderId\":\"EBAY-1\",\"orderFulfillmentStatus\":\"NOT_STARTED\","
           "\"orderPaymentStatus\":\"PAID\",\"cancelStatus\":{\"cancelState\":\"NONE\"},"
           "\"fulfillmentStartInstructions\":[{\"fulfillmentInstructionsType\":\"SHIP_TO\","
           "\"shippingStep\":{\"shipTo\":{\"fullName\":\"SECRET BUYER\"}}}],"
           "\"lineItems\":[{\"lineItemId\":\"line-1\",\"sku\":\"008\",\"quantity\":4,"
           "\"lineItemFulfillmentStatus\":\"NOT_STARTED\"}]}]}";
    if (!strcmp(url,"https://api.sandbox.ebay.com/sell/inventory/v1/inventory_item?limit=100&offset=0"))
      json="{\"total\":0,\"inventoryItems\":[]}";
  }
  if (!json) { atomic_store(&market_bad,true); return false; }
  *size=strlen(json); *body=malloc(*size+1);
  if (!*body) return false;
  memcpy(*body,json,*size+1); *status=200;
  return true;
}

static void market_set(script_page_t *page,const char *name,const char *value) {
  window_t *c=script_page_control(page,name);
  if (c) send_message(c,edSetText,0,(void *)value);
}
static void market_enter_key(script_page_t *page) {
  send_message(script_page_control(page,"secret"),evTextInput,0,(void *)market_key);
}
static bool market_result_has(script_page_t *page,const char *needle) {
  char text[4096]="";
  send_message(script_page_control(page,"results"),edGetText,sizeof(text),text);
  return strstr(text,needle)!=NULL;
}
static bool market_workbench_accepts(const char *snapshot) {
  lua_State *L=luaL_newstate();
  if (!L) return false;
  luaL_openlibs(L);
  lua_pushstring(L,snapshot);
  lua_setglobal(L,"snapshot");
  const int result=luaL_dostring(L,
    "local json=dofile('share/lua/orion/json.lua');"
    "local workbench=dofile('apps/ledger/share/addons/marketplaces/reconciliation.lua');"
    "local state=workbench.new();"
    "local data=json.decode(snapshot);"
    "assert(workbench.sync(state,data,nil,json).inserted==1);"
    "assert(workbench.sync(state,data,nil,json).unchanged==1);"
    "assert(workbench.report(state,json).orders[1].lines[1].issue=='unresolved_sku');");
  if (result!=LUA_OK) fprintf(stderr,"[ledger/workbench] %s\n",lua_tostring(L,-1));
  lua_close(L);
  return result==LUA_OK;
}
static void test_market_dialog(void) {
  TEST("marketplace native menu route and packaged form: fetch, isolated key, preview, change provider, cancel");
  bool routed=false;
  for (int i=0;i<ledger_action_meta_count;i++)
    if (!strcmp(ledger_action_meta[i].name,"file.marketplace")) routed=true;
  ASSERT_TRUE(routed);
  ASSERT_TRUE(ledger_ui_open(MARKET_SHARE));
  script_page_t *page=ledger_market_test_open(MARKET_SHARE,"share/lua/orion/json.lua",market_fake);
  ASSERT_TRUE(page && script_page_control(page,"secret") && script_page_control(page,"fetch") && script_page_control(page,"export"));
  market_set(page,"account","test-shop");
  market_set(page,"client_id","123");
  market_enter_key(page);
  char secret[64]="";
  window_t *edit=script_page_control(page,"secret");
  send_message(edit,edGetText,sizeof(secret),secret);
  ASSERT(!secret[0] && !strstr(edit->title,market_key),"generic edit read and title do not expose the key");
  ASSERT_TRUE(script_page_perform(page,"fetch"));
  ASSERT_NULL(ledger_market_test_html(page));
  ASSERT_NULL(ledger_market_test_snapshot(page));
  ASSERT_TRUE(ledger_market_test_wait(page));
  ASSERT_EQUAL(atomic_load(&market_calls),3);
  ASSERT(!atomic_load(&market_bad),"worker used only the expected authenticated endpoints");
  ASSERT_TRUE(market_result_has(page,"ORDER-01"));
  ASSERT_TRUE(market_result_has(page,"available=4"));
  const char *snapshot=ledger_market_test_snapshot(page);
  ASSERT(snapshot && strstr(snapshot,"\"observed_at\"") && strstr(snapshot,"\"id\":\"ORDER-01\"")
      && strstr(snapshot,"\"availability\":[]") && strstr(snapshot,"\"mode\":\"orders\"") &&
      !strstr(snapshot,market_key),"snapshot contains sanitized, timestamped orders only");
  ASSERT_TRUE(market_workbench_accepts(snapshot));
  ASSERT_NULL(ledger_market_test_html(page));
  ASSERT_TRUE(script_page_perform(page,"preview"));
  ASSERT_TRUE(market_result_has(page,"ozon/production/test-shop/ORDER-01"));
  const char *html=ledger_market_test_html(page);
  ASSERT(html && strstr(html,"0007") && !strstr(html,market_key),"explicit preview arms only filtered HTML");
  ASSERT_TRUE(script_page_perform(page,"choose_ebay"));
  ASSERT_TRUE(script_page_perform(page,"choose_sandbox"));
  market_enter_key(page);
  ASSERT_TRUE(script_page_perform(page,"fetch"));
  ASSERT_TRUE(ledger_market_test_wait(page));
  ASSERT_EQUAL(atomic_load(&market_calls),5);
  ASSERT_TRUE(script_page_perform(page,"preview"));
  ASSERT_TRUE(market_result_has(page,"ebay/sandbox/test-shop/EBAY-1"));
  ASSERT(!market_result_has(page,"SECRET BUYER"),"buyer data is omitted");
  snapshot=ledger_market_test_snapshot(page);
  ASSERT(snapshot && strstr(snapshot,"EBAY-1") && !strstr(snapshot,"SECRET BUYER") &&
      !strstr(snapshot,market_key),"handoff excludes provider secrets and buyer data");
  market_set(page,"account","changed-shop");
  ASSERT(!script_page_perform(page,"export"),"edited options block the snapshot save action");
  ASSERT(!script_page_perform(page,"save"),"edited options make result stale");
  atomic_store(&market_delay,true);
  market_enter_key(page);
  ASSERT_TRUE(script_page_perform(page,"fetch"));
  ASSERT_TRUE(script_page_perform(page,"stop"));
  ASSERT_NULL(ledger_market_test_html(page));
  ASSERT_TRUE(ledger_market_test_wait(page));
  ledger_market_test_close(page);
  ledger_ui_close();
  PASS();
}

int main(void) {
  TEST_START("Ledger native marketplace");
  test_env_init();
  DB_CLASS(db_schema_xml);
  DB_CLASS(ledger_ib);
  g_ledger=&market_fixture;
  market_fixture.cfg=ledger_test_writable_seed("cfg","db_schema_xml",&ledger_cfg_schema,
    MARKET_SHARE "/configurations/ru-2003/configuration.xml");
  market_fixture.ib=ledger_test_infobase(MARKET_SHARE "/configurations/ru-2003/demo.xml");
  register_database("cfg",market_fixture.cfg);
  register_database("ib",market_fixture.ib);
  snprintf(g_ledger->share_dir,sizeof(g_ledger->share_dir),"%s",MARKET_SHARE);
  atomic_init(&market_calls,0); atomic_init(&market_delay,false); atomic_init(&market_bad,false);
  test_market_dialog();
  unregister_database("ib");
  unregister_database("cfg");
  destroy_database(market_fixture.ib);
  destroy_database(market_fixture.cfg);
  test_env_shutdown();
  TEST_END();
}
