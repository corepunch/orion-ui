return function()
  local Template = {}
  function Template.html(text)
    return (tostring(text):gsub("&", "&amp;"):gsub("<", "&lt;"):gsub(">", "&gt;"):gsub('"', "&quot;"):gsub("'", "&#39;"))
  end
  function Template.render(text, fields)
    return (text:gsub("{{%s*([%w_]+)%s*}}", function(key)
      assert(fields[key] ~= nil, "missing template field: " .. key)
      return Template.html(fields[key])
    end))
  end
  return Template
end
