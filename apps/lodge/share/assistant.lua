-- The assistant window: type what you want in your own words; the answer and
-- the rows that settle it appear below. Enter asks; Enter again on the same
-- request carries out its one action. A row opens or does what it says.
local orion = require("orion")
local Engine = require("assistant_engine")
local application = require("orion.application")
local Assistant = {form = "assistant", title = orion.tr("Lodge assistant")}

function Assistant:init()
  self.text, self.asked, self.done = "", nil, ""
  self.answer = Engine.suggestions()
end
function Assistant:run(row)
  local message, page = Engine.perform(row.action, row.arg)
  self.done = message or ""
  self.asked = nil
  if page then orion.assert(application.open(page)) end
  -- What was done changes what is left: show the day's suggestions again.
  self.text = ""
  self.answer = Engine.suggestions()
end
function Assistant:ask(values)
  local text = values.ask or ""
  local first = self.answer.rows[1]
  if self.asked and self.asked == text and first and first.confirm then return self:run(first) end
  if text:find("%S") and self.asked == text and first and first.action ~= "" and first.action ~= "ask" and #self.answer.rows == 1 then return self:run(first) end
  self.text, self.done = text, ""
  self.answer = Engine.answer(text)
  self.asked = text
end
function Assistant:choose(values)
  local row = orion.assert(values.answers, orion.tr("Choose a row."))
  if row.action == "ask" then
    self.text, self.done = row.arg, ""
    self.answer = Engine.answer(row.arg)
    self.asked = row.arg
    return
  end
  orion.assert(row.action ~= "", orion.tr("This row is information only."))
  self:run(row)
end
function Assistant:suggest()
  self.text, self.asked, self.done = "", nil, ""
  self.answer = Engine.suggestions()
end
function Assistant:examples()
  self.text, self.asked, self.done = "", nil, ""
  self.answer = {reply = orion.tr("Choose an example to see how it works; change the names and dates to your own."), rows = Engine.examples()}
end
function Assistant:data()
  -- Suggestions follow the records while nothing is typed.
  if self.text == "" and not self.asked then self.answer = Engine.suggestions() end
  local rows = {}
  for i, row in ipairs(self.answer.rows) do
    rows[i] = {section = row.section, kind = row.kind, title = row.title, detail = row.detail, action = row.action, arg = row.arg}
  end
  return {values = {ask = self.text, reply = self.answer.reply, done = self.done},
    lists = {answers = rows}, placeholders = {ask = orion.tr("For example: check in Morgan · free rooms friday 2 nights")}}
end
return Assistant
