# What a small hotel needs from its software

The routine work of a hotel of roughly 5–40 rooms, run by an owner and a
few staff, and where Orion Lodge stands on each. It is Lodge's counterpart
to Ledger's accountant-request audits: the list to judge the app by.

Status: **Yes** — Lodge does it; **Partly** — a usable subset, with the gap
named; **No** — not yet. Shared mode means the multi-terminal server view
(`lodge --remote`).

## Every shift at the front desk

| Need | Status | In Lodge |
|---|---|---|
| See today's arrivals, departures and guests in house at a glance | Yes | **Today** page, three lists with counts and occupancy |
| Know what must be handled now (no-shows, overstays, unpaid departures, rooms not ready) | Yes | **Today › Needs attention** |
| Check a guest in, only into a clean room | Yes | Today or Reservations; refused if the room is not clean or still occupied |
| Take payment before departure and check the guest out | Yes | **Settle balance**, then **Check out**; check-out requires a zero balance |
| Record how the guest paid (cash, card, transfer) | Yes | Payment window's **Method**; methods are an editable catalog |
| Print a guest registration form for signature (German *Meldeschein*) | Partly | **Registration form** prints name, birth date, nationality, address, document and stay. Companions are written by hand; the legal retention and digital-signature rules (§ 30 BMG) are the hotel's process |
| Print the guest's bill | Partly | **Guest bill**: each night, extras, payments, balance. An operational bill, not a tax invoice (no VAT breakdown, numbering or tax registration) |
| Find a reservation by guest or room name | Yes | **Reservations › Find** steps through matches |
| Answer "do you have a room for these dates?" | Yes | **Availability**: free rooms for dates and guest count, cheapest first, with the stay total; reserve directly |
| Walk-in guests | Yes | Availability, booked via *Walk-in* |

## Reservations

| Need | Status | In Lodge |
|---|---|---|
| Create, change and cancel bookings without double-booking a room | Yes | Overlap and capacity checks on every write; half-open dates |
| Record where a booking came from (phone, website, portal…) | Yes | **Booked via**, an editable catalog; reported per period |
| Deposits and prepayments | Yes | Payments before arrival are held on the reservation |
| Booking confirmation for the guest | Yes | **Export confirmation** |
| Groups (several rooms, one organizer) | Partly | Two-room groups in the window, up to 100 by API; group settlement and cancellation; no group folio |
| Different prices on different nights (season, weekend, event) | Partly | Per-night overrides on a booking; no rate plans or seasons yet |
| Extra charges (breakfast, parking, minibar) | Partly | One extras amount per stay; no itemised charge lines |
| Cancellation fees and no-show charges | No | Refunds and the cancellation policy are applied by hand |
| Online booking channels (Booking.com, Expedia, own website) | No | No channel manager or booking engine |
| Email confirmations sent from the app | No | Confirmations are exported files |

## Rooms and housekeeping

| Need | Status | In Lodge |
|---|---|---|
| Room list with category, capacity and base rate | Yes | **Rooms** page |
| Clean / dirty / inspect status per room | Yes | Rooms page; check-out marks dirty |
| Daily cleaning list for housekeeping | Yes | **Cleaning list**: departures, arrivals first, stay-overs, out of service |
| Take a room out of service for repairs | Yes | **Out of service** dates; such rooms cannot be booked and leave availability |
| Assign rooms to cleaners, lost and found, maintenance tickets | No | |

## Guests

| Need | Status | In Lodge |
|---|---|---|
| Guest records with contact details | Yes | **Guests** page: phone, email, notes |
| Identity data for registration (birth date, nationality, address, document) | Yes | Guest form; birth dates in the future are refused |
| Repeat-guest history | Partly | **Stays** count per guest; no stay list on the guest yet |
| Data-protection duties (GDPR / 152-FZ): consent, export, erasure | No | Guests used by a reservation cannot be deleted; no anonymisation yet |

## Money, end of day and reports

| Need | Status | In Lodge |
|---|---|---|
| Close the day (night audit) with the day's payments and open balances | Yes | **Close day**; closed days block reservation and payment changes |
| Occupancy, ADR and RevPAR for a period | Yes | **Reports**, for any period up to a year; exportable |
| Revenue per period and payments by method | Yes | Reports: room revenue, extras, net payments per method |
| Guest-nights for statistics and tourist tax returns | Partly | Guest-nights are reported; the municipality's form and tax rate are not |
| Bookings by source and cancellations | Yes | Reports |
| VAT, tourist tax (*Kurtaxe*, курортный сбор) and statutory invoices | No | See [country readiness](country-readiness.md) |
| Fiscal cash register (German TSE / KassenSichV, Russian 54-FZ online till) | No | Card and cash payments are recorded, not fiscalised |
| Hand-over to the accountant (export to Ledger or DATEV) | No | |

## Running the business

| Need | Status | In Lodge |
|---|---|---|
| Several front-desk computers at once | Yes | Shared mode with a server, roles and retry-safe requests |
| Staff roles (owner, reception, housekeeping) | Partly | Roles in shared mode; the local app has one user |
| Audit trail of who changed what | Yes | Reservation history and payment events with actor |
| Backups and recovery | Partly | Atomic saves, `.bak` file and `--restore-backup`; no scheduled off-site backup |
| German and Russian interface | Yes | Both catalogs complete |

## Next increments, in order

1. Itemised charges on a stay (breakfast, parking, minibar) with a VAT rate per item, so the bill can become an invoice.
2. Tourist tax per guest-night with the municipality's rate and exemptions.
3. Rate plans and seasons, so a booking's nightly prices come from the calendar rather than one base rate.
4. Cancellation policy and no-show charge on the reservation.
5. Guest data protection: consent, export and anonymisation after the retention period.
6. Export of the day's payments and revenue to Ledger.
