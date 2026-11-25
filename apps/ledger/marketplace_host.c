// Native Ledger marketplace pane. Secrets never enter a Lua VM or posting code.
#include "ledger.h"
#include "marketplace_host.h"
#include "marketplace_https.h"
#include <orion/commctl/commctl.h>
#include <orion/user/i18n.h>
#include <lauxlib.h>
#include <lualib.h>
#include <time.h>

enum market_fault { MARKET_OK, MARKET_CONFIG, MARKET_HTTP, MARKET_AUTH, MARKET_LIMIT, MARKET_RESPONSE };
typedef struct {
  atomic_bool cancelled, done;
  const char *provider;
  bool sandbox;
  ledger_market_request_fn request;
  enum market_fault fault;
  unsigned requests, instructions;
  longTime_t deadline;
  size_t memory;
  char secret[TEXTEDIT_PASSWORD_CAPACITY], client[32], options[8193];
  char resources[LEDGER_PATH_MAX + 64], json_path[LEDGER_PATH_MAX * 2];
  char *summary, *preview, *html, *raw;
} market_job_t;
static struct {
  script_page_t *page;
  window_t *parent;
  uint32_t timer;
  axthread_t thread;
  market_job_t *job;
  ledger_market_request_fn request;
  const char *state;
  enum market_fault fault;
  char *summary, *preview, *html;
  bool can_save;
  char resources[LEDGER_PATH_MAX + 64], json_path[LEDGER_PATH_MAX * 2];
} market_session;

static void market_wipe(void *p, size_t size) {
  volatile unsigned char *byte = p;
  while (size--) *byte++ = 0;
}

static void market_clear_result(void) {
  free(market_session.summary); free(market_session.preview); free(market_session.html);
  market_session.summary = market_session.preview = market_session.html = NULL;
  market_session.can_save = false;
}

static void *market_lua_alloc(void *arg, void *data, size_t old, size_t size) {
  market_job_t *job = arg;
  if (!data) old = 0;
  if (!size) { free(data); job->memory -= old; return NULL; }
  if (size > old && size - old > ((size_t)64 << 20) - job->memory) return NULL;
  void *next = realloc(data,size);
  if (next) job->memory = job->memory - old + size;
  return next;
}
static bool market_expired(market_job_t *job) {
  return atomic_load(&job->cancelled) || axGetMilliseconds() >= job->deadline;
}
static void market_lua_watchdog(lua_State *L, lua_Debug *debug) {
  (void)debug;
  market_job_t *job = *(market_job_t **)lua_getextraspace(L);
  if (++job->instructions > 9000 || market_expired(job))
    luaL_error(L,"marketplace job stopped");
}
static void market_load(lua_State *L, const char *path) {
  if (luaL_loadfilex(L,path,"t") != LUA_OK) lua_error(L);
  lua_call(L,0,1);
  luaL_checktype(L,-1,LUA_TTABLE);
}
static void market_module(lua_State *L, const char *root, const char *name) {
  char path[LEDGER_PATH_MAX * 2];
  int n = snprintf(path,sizeof(path),"%s/%s.lua",root,name);
  if (n < 0 || (size_t)n >= sizeof(path)) { luaL_error(L,"marketplace module path too long"); return; }
  market_load(L,path);
}

static int market_fetch_page(lua_State *L) {
  market_job_t *job = lua_touserdata(L,lua_upvalueindex(1));
  if (++job->requests > 65 || market_expired(job)) {
    job->fault = MARKET_LIMIT;
    return luaL_error(L,"marketplace request deadline");
  }
  luaL_checktype(L,1,LUA_TTABLE);
  lua_getfield(L,1,"method"); const char *method = luaL_checkstring(L,-1);
  lua_getfield(L,1,"url"); const char *url = luaL_checkstring(L,-1);
  const char *origin = !strcmp(job->provider,"ozon") ? "https://api-seller.ozon.ru/" :
      job->sandbox ? "https://api.sandbox.ebay.com/" : "https://api.ebay.com/";
  if (strncmp(url,origin,strlen(origin))) return luaL_error(L,"marketplace origin is not authorized");
  lua_getfield(L,1,"body");
  const char *payload = NULL;
  if (!lua_isnil(L,-1)) {
    lua_getfield(L,lua_upvalueindex(2),"encode");
    lua_pushvalue(L,-2);
    lua_call(L,1,1);
    payload = luaL_checkstring(L,-1);
  }
  int status = 0; size_t size = 0;
  const char *client = job->client[0] ? job->client : NULL;
  bool received = job->request(method,url,client,job->secret,payload,&job->cancelled,&job->raw,&size,&status);
  if (!received || status < 200 || status >= 300 || !job->raw || size > 1048576) {
    job->fault = status==401 || status==403 ? MARKET_AUTH : status==429 ? MARKET_LIMIT : MARKET_HTTP;
    return luaL_error(L,"marketplace request rejected");
  }
  lua_getfield(L,lua_upvalueindex(2),"decode");
  lua_pushlstring(L,job->raw,size);
  free(job->raw); job->raw = NULL;
  lua_call(L,1,1);
  luaL_checktype(L,-1,LUA_TTABLE);
  return 1;
}

static char *market_result(lua_State *L, int idx, size_t cap) {
  if (lua_isnil(L,idx)) return NULL;
  size_t n; const char *s = luaL_checklstring(L,idx,&n);
  if (n > cap || memchr(s,0,n)) { luaL_error(L,"invalid marketplace result"); return NULL; }
  char *out = malloc(n+1);
  if (!out) { luaL_error(L,"marketplace result memory exhausted"); return NULL; }
  memcpy(out,s,n+1);
  return out;
}

static int market_run_lua(lua_State *L) {
  market_job_t *job = lua_touserdata(L,1);
  static const struct { const char *name; lua_CFunction open; } libs[] = {
    {"_G",luaopen_base},{LUA_TABLIBNAME,luaopen_table},{LUA_STRLIBNAME,luaopen_string},
    {LUA_MATHLIBNAME,luaopen_math},{LUA_UTF8LIBNAME,luaopen_utf8}
  };
  for (size_t i=0;i<ARRAY_LEN(libs);i++) {
    luaL_requiref(L,libs[i].name,libs[i].open,1); lua_pop(L,1);
  }
  const char *const removed[]={"dofile","loadfile","load","print","warn","collectgarbage"};
  for (size_t i=0;i<ARRAY_LEN(removed);i++) { lua_pushnil(L); lua_setglobal(L,removed[i]); }
  market_load(L,job->json_path); // 2
  market_module(L,job->resources,"marketplaces"); // 3
  market_module(L,job->resources,job->provider); // 4
  market_module(L,job->resources,"../fulfillment/pick_lists"); // 5
  market_module(L,job->resources,"desktop"); // 6

  job->fault = MARKET_CONFIG;
  lua_getfield(L,2,"decode"); lua_pushstring(L,job->options); lua_call(L,1,1);
  lua_getfield(L,3,"options"); lua_insert(L,-2); lua_pushvalue(L,2); lua_call(L,2,1); // 7 = validated opts
  lua_getfield(L,7,"environment");
  job->sandbox = !strcmp(luaL_checkstring(L,-1),"sandbox");
  lua_pop(L,1);
  job->fault = MARKET_RESPONSE;
  lua_getfield(L,6,"prepare");
  lua_pushstring(L,job->provider);
  lua_pushvalue(L,7); lua_pushvalue(L,2); lua_pushvalue(L,3); lua_pushvalue(L,4); lua_pushvalue(L,5);
  lua_pushlightuserdata(L,job); lua_pushvalue(L,2); lua_pushcclosure(L,market_fetch_page,2);
  lua_call(L,7,3);
  job->summary = market_result(L,-3,196608);
  job->preview = market_result(L,-2,196608);
  job->html = market_result(L,-1,8388608);
  if (!job->summary) return luaL_error(L,"missing marketplace summary");
  job->fault = MARKET_OK;
  return 0;
}

static void market_worker(void *data) {
  market_job_t *job = data;
  job->deadline = axGetMilliseconds()+75000;
  job->fault = MARKET_RESPONSE;
#if LUA_VERSION_NUM >= 505
  lua_State *L = lua_newstate(market_lua_alloc,job,(unsigned)time(NULL)^(unsigned)(uintptr_t)job);
#else
  lua_State *L = lua_newstate(market_lua_alloc,job);
#endif
  if (L) {
    *(market_job_t **)lua_getextraspace(L)=job;
    lua_sethook(L,market_lua_watchdog,LUA_MASKCOUNT,10000);
    lua_pushcfunction(L,market_run_lua); lua_pushlightuserdata(L,job);
    (void)lua_pcall(L,1,0,0); // never log raw provider data or script errors
    lua_close(L);
  }
  market_wipe(job->secret,sizeof(job->secret));
  market_wipe(job->client,sizeof(job->client));
  free(job->raw);
  atomic_store(&job->done,true);
}

static void market_reap(bool wait) {
  market_job_t *job = market_session.job;
  if (!job || (!wait && !atomic_load(&job->done))) return;
  axThreadJoin(market_session.thread);
  market_session.job = NULL; market_session.thread = NULL;
  if (market_session.page) {
    if (atomic_load(&job->cancelled)) market_session.state = "idle";
    else if (job->fault == MARKET_OK) {
      market_session.state = "ready";
      market_session.summary = job->summary; job->summary = NULL;
      market_session.preview = job->preview; job->preview = NULL;
      market_session.html = job->html; job->html = NULL;
    } else { market_session.state = "error"; market_session.fault = job->fault; }
  }
  free(job->summary); free(job->preview); free(job->html); free(job);
}

static void market_stop_timer(void) {
  if (market_session.timer) { axCancelTimer(market_session.timer); market_session.timer = 0; }
}

static result_t market_page_hook(window_t *win, uint32_t msg, uint32_t value, void *data) {
  (void)win; (void)data;
  if (msg == evDestroy) {
    market_stop_timer();
    if (market_session.job) atomic_store(&market_session.job->cancelled,true);
    market_clear_result();
    return false;
  }
  if (msg == evTimer && market_session.timer && value==market_session.timer) {
    market_reap(false);
    if (!market_session.job) {
      market_stop_timer();
      script_page_perform(market_session.page,"market_refresh");
    }
    return true;
  }
  return false;
}

static bool market_bound(lua_State *L) {
  const bool same = market_session.page && script_page_owns_state(market_session.page,L);
  if (!same) luaL_error(L,"marketplace operations belong to the built-in Marketplace window");
  return same;
}

static bool market_ascii_token(const char *value, bool numeric) {
  if (!*value) return false;
  for (const unsigned char *p=(const unsigned char *)value;*p;p++)
    if (numeric ? (*p<'0'||*p>'9') : (*p<33||*p>126)) return false;
  return true;
}

int ledger_market_start(lua_State *L, const char *provider, const char *options, const char *client) {
  if (!market_bound(L)) return 0;
  market_reap(false);
  window_t *secret = script_page_control(market_session.page,"secret");
  if (market_session.job) return luaL_error(L,"%s",ui_tr("Wait for the previous marketplace request to stop."));
  market_clear_result(); market_session.state = "idle";
  const bool is_ozon = !strcmp(provider,"ozon");
  if ((!is_ozon && strcmp(provider,"ebay")) || strlen(options)>8192 || strlen(client)>=32 ||
      (is_ozon && !market_ascii_token(client,true)) || (!is_ozon && client[0])) {
    if (secret) send_message(secret,edSetText,0,"");
    return luaL_error(L,"%s",ui_tr("Enter valid marketplace settings."));
  }
  if (!ledger_market_supported()) {
    if (secret) send_message(secret,edSetText,0,"");
    return luaL_error(L,"%s",ui_tr("Marketplace connections are unavailable in this build."));
  }
  market_job_t *job = calloc(1,sizeof(*job));
  if (!job) return luaL_error(L,"marketplace memory unavailable");
  if (!secret || !textedit_take_password(secret,job->secret,sizeof(job->secret)) || !market_ascii_token(job->secret,false)) {
    market_wipe(job->secret,sizeof(job->secret)); free(job);
    return luaL_error(L,"%s",ui_tr("Enter a valid API key or eBay access token."));
  }
  job->provider = is_ozon ? "ozon" : "ebay";
  job->request = market_session.request;
  snprintf(job->options,sizeof(job->options),"%s",options);
  snprintf(job->client,sizeof(job->client),"%s",client);
  memcpy(job->resources,market_session.resources,sizeof(job->resources));
  memcpy(job->json_path,market_session.json_path,sizeof(job->json_path));
  atomic_init(&job->cancelled,false); atomic_init(&job->done,false);
  window_t *win = script_page_window(market_session.page);
  market_session.timer = win ? axSetTimer(win,100,NULL,true) : 0;
  if (market_session.timer) market_session.thread = axThreadCreate(market_worker,job);
  if (!market_session.thread) {
    market_stop_timer(); market_wipe(job->secret,sizeof(job->secret)); free(job);
    return luaL_error(L,"%s",ui_tr("The marketplace request could not start."));
  }
  market_session.job = job; market_session.state = "loading";
  lua_pushboolean(L,true);
  return 1;
}

int ledger_market_status(lua_State *L) {
  if (!market_bound(L)) return 0;
  const char *state = market_session.state ? market_session.state : "idle";
  lua_pushstring(L,state);
  if (!strcmp(state,"ready")) lua_pushstring(L,market_session.summary);
  else if (!strcmp(state,"error")) {
    const char *message = "Marketplace response was incomplete or invalid. No preview was generated.";
    switch (market_session.fault) {
      case MARKET_CONFIG: message = "Check the account label, UTC date range and marketplace settings."; break;
      case MARKET_AUTH: message = "Marketplace authorization failed. Check the credential, expiry and read permissions."; break;
      case MARKET_LIMIT: message = "Marketplace request reached a rate or time limit. Retry with a smaller date range."; break;
      case MARKET_HTTP: message = "Could not reach the marketplace securely. Check your connection and try again."; break;
      default: break;
    }
    lua_pushstring(L,ui_tr(message));
  } else if (!strcmp(state,"loading") && market_session.job && atomic_load(&market_session.job->cancelled))
    lua_pushstring(L,ui_tr("Stopping marketplace request…"));
  else lua_pushnil(L);
  return 2;
}

bool ledger_market_cancel(lua_State *L) {
  if (!market_bound(L)) return false;
  window_t *secret=script_page_control(market_session.page,"secret");
  if (secret) send_message(secret,edSetText,0,"");
  if (market_session.job) atomic_store(&market_session.job->cancelled,true);
  else market_session.state = "idle";
  market_clear_result();
  return true;
}

bool ledger_market_paste(lua_State *L) {
  if (!market_bound(L)) return false;
  window_t *secret=script_page_control(market_session.page,"secret");
  return secret && textedit_paste_password(secret);
}

int ledger_market_preview(lua_State *L) {
  if (!market_bound(L)) return 0;
  if (!market_session.preview || !market_session.html)
    return luaL_error(L,"%s",ui_tr("No eligible pick sheet is available for this snapshot."));
  market_session.can_save = true;
  lua_pushstring(L,market_session.preview);
  return 1;
}
const char *ledger_market_printable(lua_State *L) {
  if (!market_bound(L)) return NULL;
  if (!market_session.can_save || !market_session.html)
    luaL_error(L,"%s",ui_tr("Preview this pick sheet before saving it."));
  return market_session.html;
}
bool ledger_market_supported(void) {
  return market_session.request && (market_session.request != ledger_market_https || ledger_market_https_available());
}
int ledger_market_dates(lua_State *L) {
  const time_t now=time(NULL), start=now-7*86400;
  char from[32]="", until[32]="";
  struct tm *tm=gmtime(&start); if (tm) strftime(from,sizeof(from),"%Y-%m-%dT%H:%M:%SZ",tm);
  tm=gmtime(&now); if (tm) strftime(until,sizeof(until),"%Y-%m-%dT%H:%M:%SZ",tm);
  lua_pushstring(L,from); lua_pushstring(L,until);
  return 2;
}

static script_page_t *market_open_view(const char *share, const char *json, ledger_market_request_fn request, char *error, size_t size) {
  market_reap(false);
  if (market_session.page || market_session.job) {
    snprintf(error,size,"%s",ui_tr("Wait for the previous marketplace request to stop.")); return NULL;
  }
  char path[LEDGER_PATH_MAX * 2];
  snprintf(market_session.resources,sizeof(market_session.resources),"%s/addons/marketplaces",share);
  snprintf(market_session.json_path,sizeof(market_session.json_path),"%s",json);
  snprintf(path,sizeof(path),"%s/marketplace.orion",market_session.resources);
  market_session.request = request;
  market_session.state = "idle";
  market_session.page = script_page_open(path,error,size);
  return market_session.page;
}

void ledger_market_open(window_t *parent, const char *share) {
  char error[1024]="", json[LEDGER_PATH_MAX * 2];
  snprintf(json,sizeof(json),"%s/../share/orion/lua/orion/json.lua",ui_get_exe_dir());
  if (!axPathExists(json)) snprintf(json,sizeof(json),"share/lua/orion/json.lua");
  script_page_t *page = market_open_view(share,json,ledger_market_https,error,sizeof(error));
  if (!page) { message_box(parent,error,ui_tr("Marketplace Orders"),MB_OK); return; }
  script_page_run_hook(page,parent,market_page_hook);
  market_session.page = NULL;
  script_page_close(page);
  market_reap(false);
}
script_page_t *ledger_market_test_open(const char *share, const char *json_module, ledger_market_request_fn request) {
  char error[1024]="";
  script_page_t *page=market_open_view(share,json_module,request,error,sizeof(error));
  if (!page) { fprintf(stderr,"[marketplace fixture] %s\n",error); return NULL; }
  if (!script_page_create_hook(page,NULL,market_page_hook)) { ledger_market_test_close(page); return NULL; }
  return page;
}
bool ledger_market_test_wait(script_page_t *page) {
  if (page != market_session.page) return false;
  for (int i=0; market_session.job && !atomic_load(&market_session.job->done) && i<400; ++i) axSleep(5);
  if (market_session.job && !atomic_load(&market_session.job->done)) return false;
  window_t *win=script_page_window(page);
  if (win && market_session.timer) send_message(win,evTimer,market_session.timer,NULL);
  return !market_session.job;
}
const char *ledger_market_test_html(script_page_t *page) {
  return page==market_session.page && market_session.can_save ? market_session.html : NULL;
}
void ledger_market_test_close(script_page_t *page) {
  script_page_close(page);
  market_session.page = NULL;
  market_reap(false);
}
void ledger_market_shutdown(void) {
  market_stop_timer();
  if (market_session.job) atomic_store(&market_session.job->cancelled,true);
  market_reap(true);
  market_clear_result();
}
