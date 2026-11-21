# Lodge user stories

Stories for the [personas](personas.md), grouped by the shift they belong to.
Each has acceptance criteria and its status in this release. **Done** stories
are covered by `apps/lodge/tests/lodge_test.c` (with `desk_spec.lua` and
`assistant_spec.lua`) unless noted. The *Source* column points to the theme
in the [complaints research](research-hotel-software-complaints.md) that
motivated it.

## Morning: arrivals and housekeeping

| # | Story | Acceptance criteria | Source | Status |
|---|---|---|---|---|
| M1 | As **Petra**, I want to see what needs attention when I open Lodge, so that nothing is forgotten. | Today lists overdue arrivals and departures, unsettled departures, rooms to prepare, rooms out of service and an unclosed yesterday, most urgent first; a row opens the page that settles it. | 1, 12 | Done |
| M2 | As **Ana**, I want a printed cleaning list, so that I know what to do in each room. | One row per room with departure clean, stay-over tidy, arrival-first or out of service. | 13 | Done |
| M3 | As **Jonas**, I want to mark a room clean from the desk in one action. | Housekeeping tab *Mark clean*; the assistant's "mark 104 clean"; Today's arrival turns from *Room not ready* to *Room ready*. | 13 | Done |
| M4 | As **Jonas**, I want check-in to refuse a room that is not ready, so that no guest walks into a dirty room. | Check-in into a room that is not clean is refused with "Mark the room clean before checking in." | 13 | Done |

## Day: bookings and the desk

| # | Story | Acceptance criteria | Source | Status |
|---|---|---|---|---|
| D1 | As **Jonas**, I want to check a guest in with two actions from Today. | Select the arrival, press *Check in* (or double-click, or Ctrl+I). | 1, 14 | Done |
| D2 | As **Jonas**, I want to book a walk-in in one window. | Name, nights and guests; rooms free from tonight with ready rooms first; *Check in now* creates the guest, the stay, the check-in and the payment in one transaction, or nothing. | 14 | Done |
| D3 | As **Petra**, I want to see who sleeps where over the next two weeks. | Room calendar: a row per room and a column per night with real dates; in house, booked and out-of-service nights marked; previous/next week; double-click a room books its first free night. | 1 | Done (moving a stay by drag is not yet supported; edit dates in the editor) |
| D4 | As **Jonas**, I want to book a guest who is not on file without leaving the reservation. | *Or new guest* field in the editor and in the calendar's free-room search; an existing guest with the same name is reused. | 1 | Done |
| D5 | As **Petra**, I want to find any reservation by name, room, phone or number. | Find box and *Find next* (F3) on Reservations; the assistant finds by any of these and opens the booking. | 16 | Done |
| D6 | As **Jonas**, I want to extend or shorten a stay by a night. | *Extend stay* (Ctrl+E) on Today and Reservations, *Shorten stay* on Reservations; refused when it would overlap the next booking or leave no night. | 1 | Done |
| D7 | As **Petra**, I want a guest who never came to be marked as a no-show, so that the room is free and the figures are honest. | *No-show* from the arrival day: the stay is cancelled with the note "No-show", payments stay for a separate refund. | 12 | Done |
| D8 | As **Petra**, I want deposits and balances visible on every reservation. | Total, paid and balance columns on Reservations; balance on Today's lists; payment window shows the balance. | 11 | Done |
| D9 | As **Jonas**, I want to ask in my own words. | The assistant understands check-in/out, payments, free rooms, bookings, lists, housekeeping, reports, extend, no-show, walk-in, close day and search, in English, German and Russian; Enter asks, Enter again does it. | 4 | Done |
| D10 | As **Jonas**, I want every daily command on the keyboard. | Ribbon shortcuts: Ctrl+N, Ctrl+Shift+N, Ctrl+I, Ctrl+O, Ctrl+E, Ctrl+M, Ctrl+B, Ctrl+K, Ctrl+S, F3, F5, Alt+Left/Right. | 16 | Done |
| D11 | As **Sergei**, I want group bookings with one statement and one settlement. | Group reservation, group statement, group settlement and group cancellation with preview. | 16 | Done (two rooms per group in the window; the service accepts up to 100) |

## Evening: departures, money and the night audit

| # | Story | Acceptance criteria | Source | Status |
|---|---|---|---|---|
| E1 | As **Jonas**, I want to check a guest out only when the bill is settled. | Check-out refused with a balance; *Settle balance* opens the payment window with the balance. | 11 | Done |
| E2 | As **Petra**, I want a guest bill and a registration form with my hotel's name. | Bill, registration form, confirmation and lists carry the letterhead from Setup; registration shows the check-out time. | 7 | Done |
| E3 | As **Petra**, I want the night audit as one checklist. | Arrivals, departures, unsettled bills, rooms to clean, guests in house, money taken today by method, outstanding balance and cancelled stays holding payments; *Close today* only when no red item is left; closed days are locked. | 12 | Done |
| E4 | As **Jonas**, I want a printed front desk list, in case the computer is busy or off. | Today's arrivals, departures and guests in house with rooms, dates and balances. | 9 | Done |

## Month end and setup

| # | Story | Acceptance criteria | Source | Status |
|---|---|---|---|---|
| S1 | As **Mira**, I want the period's figures. | Occupancy, guest-nights, room revenue, ADR, RevPAR, payments by method, bookings by source; this month, last month or any period up to a year; printable. | 6 | Done |
| S2 | As **Mira**, I want the records as CSV. | Reservations, guests and payments as CSV with stable column names; values that look like formulas are quoted. | 6, 10 | Done |
| S3 | As **Petra**, I want to try Lodge with sample data before entering my hotel. | *Add sample hotel* in Setup (only for an empty, configured hotel): nine rooms, guests and stays dated around today. | 1 | Done |
| S4 | As **Petra**, I want my hotel's details on documents. | Name, address, phone, email, check-in and check-out times in Setup. | 7 | Done |
| S5 | As **Sergei**, I want Lodge fully in Russian. | Every page, ribbon command, document and assistant answer translated; assistant understands Russian. | 16 | Done |

## Next

| # | Story | Why not yet |
|---|---|---|
| N1 | Seasonal and weekday rates with minimum stays (research theme 5) | Needs a rate-plan model and migration; nightly prices per reservation exist today. |
| N2 | CSV import from another PMS (theme 10) | Export is done; import needs mapping and validation screens. |
| N3 | Automatic dated backups (theme 10) | The XML database keeps one `.bak`; a dated rotation belongs in the framework's datasource. |
| N4 | Cash drawer count per shift (theme 11) | Payments by method per day are in the night audit; a counted-versus-expected step is not. |
| N5 | City tax and statutory invoices (themes 7, country readiness) | Country-specific; see [country readiness](country-readiness.md). |
| N6 | Drag to move a stay in the calendar (theme 1) | Needs cell hit-testing in TableView; edit dates in the editor meanwhile. |

## Out of scope

Channel management (OTA sync), online payment processing, a booking engine,
revenue management and POS. Lodge prevents double booking itself on every
write; a hotel that sells on booking portals closes inventory there, or uses a
separate channel manager.
