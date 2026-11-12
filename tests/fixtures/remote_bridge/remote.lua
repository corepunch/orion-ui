local native = require('orion.remote_native')
local Page = {form = 'remote'}
function Page:init() self.result = 'waiting' end
function Page:connect()
  local id, err = native.request(self.app.handle, 'GET', '', function(status)
    self.result = tostring(status)
  end)
  assert(id, err)
end
function Page:data() return {values = {result = self.result}} end
return Page
