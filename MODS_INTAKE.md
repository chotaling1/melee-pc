# Feature intake

How a mod idea becomes a ticket in `MODS_BACKLOG.md`. The point is that Chuck
decides the shape of a feature and the agent decides how to build it — not the
other way around.

Run this whenever Chuck raises a new feature, even a one-line one. Skip it only
for changes to a ticket that already exists (those become log entries on that
ticket, as they do today).

## Procedure

1. **Recon first, questions second.** Spend a few minutes finding what already
   exists: the files, the functions, the current behavior. Cheap grep-level
   recon, not a design. This keeps the questions concrete and stops the agent
   from asking things the code already answers.
2. **Ask the open questions in one batch.** Work the checklist below and ask
   only what Chuck hasn't already said — a feature description usually answers
   half of it. Never walk through all eight one at a time.
3. **Write the ticket** in the template below and commit it to `mods`. No
   implementation in the same pass.
4. **Anything still unresolved goes in the ticket as `Assumed:`**, so it reads
   as a decision Chuck can overturn instead of disappearing into the plan.

## Checklist

1. **Player-facing shape** — where does this appear, and how does someone turn
   it on? A Special Melee entry, a rules toggle, a launcher pref, a stage, or
   always on?
2. **Blast radius** — is it sealed inside its own mode, or does it show up in
   vanilla modes too? Name the screens it must *not* leak into.
3. **v1 boundary** — what is the smallest version worth actually playing? What
   is explicitly deferred to later?
4. **Rules interactions** — stocks, timer, results screen, items, teams,
   respawns: what does this override, and what stays vanilla?
5. **Assets** — does it need art, models or audio? Is drawn-from-code fine for
   v1, or does it have to look finished? (No Nintendo assets ship, ever — see
   the red lines in `AGENTS.md`.)
6. **Feel constraints** — is anything about physics, frame timing or float
   behavior allowed to change, or is this strictly additive/cosmetic?
7. **Done means** — what does Chuck do on the Windows build to call it working?
   A concrete in-game check, not "it builds".
8. **Priority** — next up, or queued behind what's already in the backlog?

## Ticket template

```markdown
### <Name> (queued <YYYY-MM-DD>)
Goal: <one or two sentences, described as the player experiences it>

Where it stands today:
- <what already exists, with file:line>

Work:
1. <step, with the files it touches>
2. ...

Assumed (correct me): <anything decided without Chuck>
Deferred: <explicit non-goals for v1>
Verify: <build command, then what Chuck checks in game>
```

Keep the existing conventions: newest decisions first within a ticket, status
and follow-ups appended as dated lines (`Decided (Chuck, 2026-09-17): ...`,
`Status 2026-09-17: ...`).

## Risk flagging

If recon turns up something that could eat the whole ticket — missing menu art,
a table that can't be extended, a structure that would have to change on disc —
say so in the ticket as the first work item, phrased as a thing to settle
*before* the rest is worth building. A risk buried at the bottom of a plan is a
risk nobody acts on.
