-- Plugin: Hinweis auf Kleinbetragsrechnungen (§ 33 UStDV, bis 250 € brutto):
-- sie brauchen keine USt-IdNr. des Kunden und keinen gesonderten
-- Steuerbetrag; die Quittung reicht. Das Plugin protokolliert nur.
ledger.on("post", function(ctx)
  if ctx.type.code == "outgoing_invoice" and ctx.document.total <= 250 then
    ledger.log(string.format("%s: Kleinbetragsrechnung (%.2f EUR), § 33 UStDV", ctx.document.number, ctx.document.total))
  end
end)
