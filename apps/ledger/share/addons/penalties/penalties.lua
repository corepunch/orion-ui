-- Penalties on overdue receivables. Payments settle a customer's oldest debt
-- first; whatever is still unpaid a payment term after its document is
-- overdue, and the penalty is a percentage of it per day. Posting writes one
-- manual operation with an entry per overdue debt.
local orion = require("orion")
local Model = require("orion.model")
local ledger = require("ledger")
local Plugins = require("orion.plugins")
local calendar = Plugins.use("calendar", 1)
local money = Plugins.use("money", 1)

local Documents = Model:extend("ib.documents")
local Entries = Model:extend("ib.entries")
local DocTypes = Model:extend("cfg.doc_types")

-- Where each configuration keeps receivables and accrues the penalty.
local ACCOUNTS = {
	["ru-2003"] = {receivable = "62", debit = "76", credit = "91.01"},
	["ru-2026"] = {receivable = "62", debit = "76", credit = "91.01"},
	["de-2003"] = {receivable = "1400", debit = "1400"},
	["de-2026"] = {receivable = "1400", debit = "1400", credit = "2650"},
}

local addDays, daysBetween, round2 = calendar.add, calendar.between, money.round

local function accountId(code)
	local account = code and ledger.account(code)
	return account and account.id
end

-- The unpaid part of each debit on the account, per counterparty; payments
-- (and advances, until the next debit) settle the oldest first.
local function openDebts(account, date)
	local debts, advance, order = {}, {}, {}
	for _, m in ipairs(ledger.movements(account, {to = date})) do
		local list = debts[m.subconto]
		if not list then
			list = {}
			debts[m.subconto], advance[m.subconto] = list, 0
			table.insert(order, m.subconto)
		end
		local amount = m.debit - math.min(m.debit, advance[m.subconto])
		advance[m.subconto] = advance[m.subconto] - (m.debit - amount)
		local last = list[#list]
		if amount > 0 and last and last.document == m.document then
			last.amount = last.amount + amount  -- one debt per document, whatever its lines
		elseif amount > 0 then
			table.insert(list, {document = m.document, number = m.number, date = m.date, amount = amount})
		end
		local paid = m.credit
		while paid > 0 and list[1] do
			local part = math.min(list[1].amount, paid)
			list[1].amount, paid = list[1].amount - part, paid - part
			if list[1].amount < 0.005 then table.remove(list, 1) end
		end
		advance[m.subconto] = advance[m.subconto] + paid
	end
	return debts, order
end

local Penalties = {form = "penalties", title = orion.tr("Penalties on Overdue Receivables")}

function Penalties:init()
	local defaults = ACCOUNTS[ledger.configuration] or {}
	local _, periodEnd = ledger.period()
	self.date = periodEnd ~= "" and periodEnd or orion.today()
	self.term = 10
	self.rate = 0.1
	self.receivable = accountId(defaults.receivable)
	self.debit = accountId(defaults.debit)
	self.credit = accountId(defaults.credit)
	self.lines = self.receivable and self:overdue() or {}
end

function Penalties:overdue()
	local lines = {}
	local debts, order = openDebts(self.receivable, self.date)
	for _, counterparty in ipairs(order) do
		for _, debt in ipairs(debts[counterparty]) do
			local due = addDays(debt.date, self.term)
			local days = daysBetween(due, self.date)
			if days > 0 and counterparty ~= 0 then
				table.insert(lines, {
					counterparty_id = counterparty, number = debt.number, due = due, days = days,
					debt = round2(debt.amount), penalty = round2(debt.amount * self.rate / 100 * days),
				})
			end
		end
	end
	return lines
end

function Penalties:comment()
	return string.format(orion.tr("Penalties on overdue receivables on %s"), orion.formatDate(self.date))
end

function Penalties:data()
	local total = 0
	for _, line in ipairs(self.lines) do total = total + line.penalty end
	return {
		values = {
			date = self.date, term = self.term, rate = self.rate,
			receivable = self.receivable, debit = self.debit, credit = self.credit,
			summary = string.format(orion.tr("Overdue debts: %d. Penalties: %s."), #self.lines, orion.formatNumber(total, 2)),
		},
		lists = {lines = self.lines},
		disabled = {accrue = #self.lines == 0},
	}
end

function Penalties:calculate(values)
	self.date = orion.assert(values.date, orion.tr("Enter the date to calculate on."))
	self.term = values.term or 0
	self.rate = values.rate or 0
	self.receivable = orion.assert(values.receivable, orion.tr("Choose the receivables account."))
	self.debit, self.credit = values.debit, values.credit
	if self.term < 0 then orion.fail(orion.tr("The payment term cannot be negative.")) end
	if self.rate <= 0 then orion.fail(orion.tr("Enter the penalty rate.")) end
	self.lines = self:overdue()
end

function Penalties:accrue(values)
	self:calculate(values)
	if #self.lines == 0 then orion.fail(orion.tr("Nothing is overdue on this date.")) end
	if not self.debit or not self.credit then orion.fail(orion.tr("Choose the accounts to post the penalties to.")) end
	local posted = Documents:find{comment = self:comment()}
	if posted then
		orion.fail(string.format(orion.tr("Penalties on this date are already posted in %s."), posted.number))
	end
	local operation = DocTypes:select(function(t) return t.form == "operation" and t.code ~= "opening" end)[1]
	if not operation then orion.fail(orion.tr("The configuration has no manual operation.")) end
	local debitAnalytics = ledger.account(self.debit).analytics == "counterparties"
	local creditAnalytics = ledger.account(self.credit).analytics == "counterparties"
	Documents:transaction(function()
		local doc = orion.assert(Documents:create{doc_type_id = operation.id, date = self.date, comment = self:comment()})
		for _, line in ipairs(self.lines) do
			orion.assert(Entries:create{
				document_id = doc.id, debit_id = self.debit, credit_id = self.credit, amount = line.penalty,
				debit_subconto_id = debitAnalytics and line.counterparty_id or 0,
				credit_subconto_id = creditAnalytics and line.counterparty_id or 0,
				content = string.format(orion.tr("Penalty on %s, %d days overdue"), line.number, line.days),
			})
		end
		orion.assert(ledger.post(doc.id))
	end)
	self.app.close(1)
end

return Penalties
