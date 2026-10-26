-- Keep the grammar usable in standalone Lua; the Ledger host supplies catalogs.
local ok, orion = pcall(require, 'orion')
return ok and orion.tr or function(text) return text end
