local native = require("orion.window_native")
local Window = {}
function Window.new(handle)
  return {
    handle = handle,
    close = function(result) return native.close(handle, result or 1) end,
    focus = function(control) return native.focus(handle, control) end,
    refresh = function() return native.refresh(handle) end,
    text = function(control) return native.text(handle, control) end,
    setText = function(control, text) return native.setText(handle, control, text) end,
    show = function(control, visible) return native.show(handle, control, visible) end,
    enable = function(control, enabled) return native.enable(handle, control, enabled) end,
  }
end
return Window
