-- Exercise the actual CLI + curl argv/headers boundary with an offline fake.
local repo = assert(arg[0]:match("^(.*)apps/ledger/tests/marketplace_transport_test%.lua$"))
if package.config:sub(1,1) ~= "/" then
  print("POSIX marketplace transport test skipped")
  return
end
local function sh(s)
  return "'" .. s:gsub("'","'\\''") .. "'"
end
local function put(path,s)
  local f=assert(io.open(path,"wb")); assert(f:write(s)); assert(f:close())
end
local function read(path)
  local f=assert(io.open(path,"rb")); local s=f:read("*a"); f:close(); return s
end
local m=assert(io.popen("mktemp -d","r"))
local root=assert(m:read("*l")); assert(m:close())
local stub=[=[#!/bin/sh
printf '%s\n' "$@" >> "$MARK_DIR/curl-argv"
case "$MARK_CASE" in
redirect) printf '%s\n302' '{"private":"DO NOT LEAK"}';;
failure) printf '%s\n401' '{"private":"DO NOT LEAK"}';;
success) printf '%s\n200' '{"total":0,"orders":[]}';;
*) exit 1;;
esac
]=]
local function execute(command)
  local ok = os.execute(command)
  return ok == true or ok == 0
end
local function test()
  local secret="FAKE-DEVELOPER-TOKEN-123"
  local auth=root .. "/credentials with spaces"
  put(root .. "/curl",stub)
  put(auth,"Authorization: Bearer " .. secret .. "\n")
  put(root .. "/options.json",'{"account":"test","environment":"sandbox","mode":"orders"}')
  assert(execute("chmod 700 " .. sh(root .. "/curl") .. " && chmod 600 " .. sh(auth)))
  -- In developer environments the interpreter can be texlua; otherwise use Lua 5.4.
  local command = "command -v lua5.4 || command -v lua || command -v texlua"
  local which=assert(io.popen(command,"r")); local lua=assert(which:read("*l")); assert(which:close())
  local function run(state,path)
    local cmd="PATH=" .. sh(root) .. ':"$PATH" MARK_DIR=' .. sh(root)
      .. " MARK_CASE=" .. sh(state) .. " ORION_EBAY_HEADERS=" .. sh(path)
      .. " " .. sh(lua) .. " " .. sh(repo .. "apps/ledger/tools/marketplace.lua")
      .. " ebay snapshot " .. sh(root .. "/options.json")
      .. " >" .. sh(root .. "/output") .. " 2>" .. sh(root .. "/errors")
    local ok=execute(cmd)
    return ok,read(root .. "/output"),read(root .. "/errors")
  end
  local ok,out,err=run("success",auth)
  assert(ok and out:find('"orders":[]',1,true) and err=="",err)
  local args=read(root .. "/curl-argv")
  assert(args:find("@" .. auth,1,true) and not args:find(secret,1,true))
  assert(args:find("\n--noproxy\n*\n",1,true) and not args:find("\n--location",1,true))
  for _, c in ipairs({"redirect","failure"}) do
    ok,out,err=run(c,auth)
    assert(not ok and out=="" and not err:find("DO NOT LEAK",1,true))
    assert(err:find(c=="redirect" and "redirect refused" or "token expiry",1,true),err)
  end
  args=read(root .. "/curl-argv")
  assert(execute("chmod 644 " .. sh(auth)))
  ok,out,err=run("success",auth)
  assert(not ok and out=="" and err:find("mode 600 or 400",1,true),err)
  assert(execute("chmod 600 " .. sh(auth) .. " && ln -s " .. sh(auth) .. " " .. sh(root .. "/link")))
  ok,out,err=run("success",root .. "/link")
  assert(not ok and out=="" and err:find("not symlink",1,true),err)
  put(auth,"Authorization: Bearer " .. secret .. "\nHost: attacker.invalid\n")
  ok,out,err=run("success",auth)
  assert(not ok and out=="" and err:find("only an eBay",1,true) and not err:find(secret,1,true),err)
  assert(read(root .. "/curl-argv")==args,"invalid credential files did not reach curl")
end
local ok, why = pcall(test)
execute("rm -rf " .. sh(root))
assert(ok,why)
print("Marketplace transport: CLI authorization file, no secret argv, rejected redirects, non-leaking HTTP errors and invalid credential files passed")
