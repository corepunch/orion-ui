-- Compile authored aliases into interned words, role records and a phrase trie.
local normalize = require('normalize')
local D = {}; D.__index=D
local schemas={
  verbs='groups',nouns='groups',adjectives='groups',fields='groups',comparisons='groups',
  relations='groups',directions='groups',ordinals='numbers',numbers='numbers',scales='numbers',
  multipliers='numbers',pronouns='list',noise='list',units='list',number_conjunctions='list',
}
local function fail(message) error('assistant dictionary: '..message,0) end
local function sequence(value,label)
  if type(value)~='table' then fail(label..' must be an alias list') end
  local count=0
  for k in pairs(value) do
    if type(k)~='number' or k<1 or k%1~=0 then fail(label..' must be a dense alias list') end
    count=count+1
  end
  if count==0 then fail(label..' must be a nonempty dense alias list') end
  for i=1,count do
    if value[i]==nil then fail(label..' must be a nonempty dense alias list') end
  end
  return value
end
function D.compile(source)
  if type(source)~='table' then fail('expected a vocabulary table') end
  local words={}
  local function intern(alias,role,value)
    if type(alias)~='string' then fail(role..' aliases must be strings') end
    local ok,text=pcall(normalize,alias)
    if not ok then fail(role..' alias is not valid UTF-8') end
    local tokens={}
    for token in text:gmatch('%S+') do
      if token:find('[%c",.!?;]') then fail('alias contains tokenizer punctuation: '..alias) end
      tokens[#tokens+1]=token
    end
    if #tokens==0 then fail(role..' alias must not be empty') end
    local key=table.concat(tokens,' ')
    local entry=words[key]
    if not entry then entry={text=key,tokens=tokens,roles={}}; words[key]=entry end
    local old=entry.roles[role]
    if old~=nil and old~=value then
      fail(('conflicting %s meanings for %q: %s / %s'):format(role,key,tostring(old),tostring(value)))
    end
    entry.roles[role]=value
  end
  for role,values in pairs(source) do
    local schema=schemas[role]
    if not schema then fail('unknown vocabulary role: '..tostring(role)) end
    if type(values)~='table' then fail(role..' must be a table') end
    if schema=='list' then
      for _,alias in ipairs(sequence(values,role)) do intern(alias,role,true) end
    elseif schema=='numbers' then
      for alias,value in pairs(values) do
        if type(value)~='number' or value~=value or math.abs(value)==math.huge or value<0 or value%1~=0 then
          fail(role..' meanings must be finite nonnegative integers')
        end
        if role~='numbers' and value==0 then fail(role..' meanings must be positive') end
        intern(alias,role,value)
      end
    else
      for value,aliases in pairs(values) do
        if type(value)~='string' or value=='' then fail(role..' meanings must be nonempty strings') end
        for _,alias in ipairs(sequence(aliases,role..'.'..value)) do intern(alias,role,value) end
      end
    end
  end
  local keys={}
  for key in pairs(words) do keys[#keys+1]=key end
  table.sort(keys)
  local self=setmetatable({words=words,entries={},root={children={}},max_depth=0},D)
  for id,key in ipairs(keys) do
    local entry=words[key]; entry.id=id; self.entries[id]=entry
    local node=self.root
    for _,token in ipairs(entry.tokens) do
      node.children[token]=node.children[token] or {children={}}
      node=node.children[token]
    end
    node.id=id
    self.max_depth=math.max(self.max_depth,#entry.tokens)
  end
  return self
end

function D:lookup(tokens,start)
  local node,entry,length=self.root,nil,0
  for i=start,math.min(#tokens,start+self.max_depth-1) do
    local token=tokens[i]
    if token.quoted then break end
    node=node.children[token.text]
    if not node then break end
    if node.id then entry,length=self.entries[node.id],i-start+1 end
  end
  return entry,length
end
return D
