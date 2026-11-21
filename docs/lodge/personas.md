# Lodge personas

Lodge starts with small hotels: guesthouses, inns and B&Bs with roughly 5–40
rooms, run by the owners and a few staff, without an IT department. The
personas below come from the
[research on hotel software complaints](research-hotel-software-complaints.md)
and from [what a small hotel needs](small-hotel-needs.md). Each one names the
Lodge features designed for them; the [user stories](user-stories.md) trace
those features to acceptance tests.

## Petra, owner-operator (primary)

*Owns a 12-room guesthouse by a lake in Bavaria with her husband. Does the
desk in the morning, the books on Sunday, and everything else in between.*

| | |
|---|---|
| Goals | Know at a glance who arrives, who leaves and who still owes money. Close the day in five minutes. Hand clean figures to the tax adviser each month. |
| Frustrations | Her previous cloud PMS charged per booking and raised its price twice; screens were slow on the village's connection; support answered by email two days later. Rate rules needed a separate rate for every weekend. |
| Skills | Confident with email and spreadsheets; does not want to learn "hotel software". |
| Context | One desk PC, sometimes a laptop in the kitchen. Internet drops when it storms. |
| What Lodge gives her | **Today** as the start page with a *Needs attention* list; the **night audit checklist**; **CSV export** of reservations, guests and payments for the adviser; offline-first local data she owns; the hotel's letterhead on every document. |

## Jonas, part-time receptionist

*A student who works the evening desk three days a week. Started last month
after a 30-minute introduction.*

| | |
|---|---|
| Goals | Check guests in and out without calling Petra. Handle a walk-in at 22:00 without mistakes. |
| Frustrations | Too many windows and clicks; forgets where commands live; afraid of breaking something. |
| Skills | Fast with a keyboard; types more than he clicks. |
| What Lodge gives him | **Ribbon** with the everyday commands on **Home** and shortcuts (Ctrl+I check in, Ctrl+O check out, Ctrl+M payment, Ctrl+Shift+N walk-in); **Walk-in** in one window; the **assistant** for "check in Morgan" or "who owes"; refusals that say what to do ("Mark the room clean before checking in."). |

## Ana, housekeeper

*Cleans the rooms every morning with one helper. Doesn't use the computer
much, but reads the paper list and tells the desk when rooms are ready.*

| | |
|---|---|
| Goals | A list of what to do in each room today. A quick way for the desk to mark rooms clean. |
| Frustrations | Guests sent to rooms that were not ready; no idea which rooms are departures and which are stay-overs. |
| What Lodge gives her | The printable **cleaning list** (departure clean, stay-over tidy, arrival first); **Housekeeping** tab with *Mark clean / dirty / needs inspection*; check-in refuses a room that is not clean; the assistant's "dirty rooms" and "mark 104 clean". |

## Sergei, owner of a mini-hotel in Russia

*Runs a 20-room mini-hotel in Kazan with two receptionists on shifts and a
part-time accountant.*

| | |
|---|---|
| Goals | Everything in Russian, amounts in rubles, a guest register that satisfies registration rules, and a clean hand-off between shifts. |
| Frustrations | Software translated badly or only half; reports that cannot be exported; double bookings after a manual change. |
| What Lodge gives him | Complete **Russian catalog** (UI, documents and the assistant, which understands Russian requests); RUB currency setup; overlap checks on every write; **shared company** mode for several desks; CSV export. See [country readiness](country-readiness.md) for the legal documents still open. |

## Mira, the accountant (secondary)

*External bookkeeper for several small hotels. Sees Lodge's output, rarely
Lodge itself.*

| | |
|---|---|
| Goals | Payments by day and method, room revenue by period, and records she can import. |
| What Lodge gives her | The **period report** (occupancy, ADR, RevPAR, payments by method, bookings by source) and **CSV files** with stable English column names, quoted for spreadsheets and safe against formula injection. |

## Anti-persona: a 300-room chain hotel

Lodge does not try to be a chain PMS: no channel manager, revenue management,
central reservations or POS. Those are deliberate exclusions, recorded in the
[user stories](user-stories.md#out-of-scope).
