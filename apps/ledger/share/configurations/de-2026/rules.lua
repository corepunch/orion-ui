-- Regeln der Konfiguration de-2026, die über die Tabellen von configuration.xml
-- hinausgehen. Die Plattform ruft die Funktionen über die Felder script= von
-- Belegtypen, Lohnabzügen und Abschlussregeln auf (apps/ledger/ledger_script.c,
-- COMPLIANCE.md). Gesetzesänderungen sind Änderungen dieser Datei und der
-- configuration.xml, kein neuer Programmstand.

-- Solidaritätszuschlag 5,5 % der Lohnsteuer (§§ 3, 4 SolZG). Freigrenze 2026:
-- 20.350 € Jahreslohnsteuer in Steuerklasse I, monatlich 1.695,83 €;
-- Milderungszone: höchstens 11,9 % des die Freigrenze übersteigenden Betrags.
-- Die Lohnsteuer kommt aus der vorher berechneten Zeile mit code "lst".
function soli(ctx)
  local lst = ctx.taxes.lst or 0
  local exempt = 20350 / 12
  if lst <= exempt then return 0 end
  return ledger.round2(math.min(lst * 0.055, (lst - exempt) * 0.119))
end

-- Pflegeversicherung Arbeitnehmeranteil mit Zuschlag für Kinderlose
-- (§ 55 Abs. 3 SGB XI: + 0,6 Prozentpunkte ab 23 Jahren ohne Kinder).
-- Beispiel für eine Regel, die Mitarbeiterdaten liest: ohne Kinderfeld im
-- Schema gilt der Zuschlag für Mitarbeiter, deren Position "(kinderlos)" nennt.
-- Einschalten mit script="pv_an_kinderlos" statt der Zeile pv_an.
function pv_an_kinderlos(ctx)
  local base = math.min(ctx.line.amount, ctx.tax.cap_month)
  local rate = ctx.tax.rate
  if string.find(ctx.employee.position or "", "(kinderlos)", 1, true) then rate = rate + 0.6 end
  return ledger.round2(base * rate / 100)
end
