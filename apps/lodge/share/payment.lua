local orion = require("orion")
local Hotel = require("hotel")
local money = require("orion.plugins").use("money", 1)
local Payment = {form = "payment", title = orion.tr("Payment or refund")}

function Payment:init()
  self.id = orion.assert(Hotel.pending_payment_id, orion.tr("Choose a reservation."))
  self.amount, self.notice, self.completed = 0, "", false
end
function Payment:record(values, sign)
  orion.assert(not self.completed, orion.tr("This payment has already been recorded."))
  local amount = values.amount
  orion.assert(Hotel.valid_amount(amount) and amount > 0, orion.tr("Enter a positive amount with at most two decimal places."))
  local minor = sign * money.minor(amount)
  if self.pending_minor then
    orion.assert(self.pending_minor == minor, orion.tr("Retry the same amount and action, or close this window to start another request."))
  else
    self.key, self.pending_minor = Hotel.next_payment_key(), minor
  end
  self.amount = amount
  orion.assert(Hotel.payment(self.id, minor, self.key))
  self.completed, self.notice = true, sign > 0 and orion.tr("Payment recorded.") or orion.tr("Refund recorded.")
end
function Payment:pay(values) self:record(values, 1) end
function Payment:refund(values) self:record(values, -1) end
function Payment:data()
  local row = orion.assert(Hotel.reservations.model:find(self.id), orion.tr("Choose a reservation."))
  return {values = {reservation = string.format(orion.tr("Reservation %d"), self.id),
    balance = string.format(orion.tr("Balance: %s"), orion.formatNumber(Hotel.balance(row), 2)),
    amount = self.amount, notice = self.notice}, disabled = {pay = self.completed, refund = self.completed}}
end
return Payment
