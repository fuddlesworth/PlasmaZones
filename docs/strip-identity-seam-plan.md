<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Strip identity across the daemon/compositor seam

The scrolling strip breaks on virtual desktops other than the one the daemon
started on. It has broken there three or four times, in different clothes, and
each fix has landed in the daemon. This document argues the recurrence is one
structural defect at the D-Bus seam, and proposes a staged repair.

**Status: the mechanism of the current failure is NOT yet confirmed.** Three
candidates survive source reading and they are not distinguishable without
instrumentation. Stage 1 is deliberately designed to repair all three, and the
instrumentation in [Gate](#gate-before-any-code) must run before any of it
lands. See [Candidates](#what-actually-breaks-three-live-candidates).

## Premise

The engine keys a strip by `(screen, desktop, activity)`.
`PhosphorEngine::PlacementStateKey` (`EngineTypes.h:21`) is that triple, and
`ScrollEngine::currentKeyForScreen()` resolves it per screen. Persisted state
agrees; a healthy two-desktop session holds two strips:

```
WindowTracking/ScrollStrips
  'LG Electronics:LG Ultra HD:115107|1|'  columns=5
  'LG Electronics:LG Ultra HD:115107|2|'  columns=5
```

`PhosphorProtocol::TileRequestEntry` (`AutotileTypes.h`) has **no desktop and
no activity field**. The D-Bus signature is `a(siiiissbbbbssiiibsb)` and every
member is per window or per screen. So the daemon resolves N strips per output
and emits them all on one screen-keyed channel that cannot say which strip a
batch belongs to.

The compositor side matches the wire, not the model. The state demonstrably
implicated in the current failure:

| State | Where | Key | Role |
| --- | --- | --- | --- |
| `m_scrollVisualDelta` | `plasmazoneseffect.h:2515` | window id | paint position of a parked column |
| `StripViewAnimator::m_motions` | `stripviewanimator.h:220` | `LogicalOutput*` | the view offset added to every carried window |

`scroll_clip.cpp:221` states the shape of the problem outright:

> `scrollManagedOutputFor` applies neither a desktop nor an activity term, so an
> off-activity column stays scroll-managed exactly like an off-desktop one does
> (#808).

**Therefore: strip-scoped compositor state can outlive the strip it describes,
with nothing on the wire able to say so.**

Deliberately *not* claimed here: that the screen-keyed sets
(`m_scrollingScreens`, `m_scrollCropStraddlerScreens`,
`m_scrollVerticalAxisScreens`) are part of this. They are re-pushed by the
daemon on context change and may be correctly maintained. They are listed in an
earlier draft of this document without evidence; that was overreach.

Also not claimed: that the engine side is uniformly correct. `m_lastAppliedRect`
(`engine_apply.cpp`, read at :330) is keyed by window id alone on the engine
object, with no context term, and is the baseline the `viewDelta` evidence loop
compares against. Disjoint per-desktop window sets hide it today; a sticky or
all-desktops window collides. That is the same defect class living inside the
engine and should be fixed alongside, but it is not the reported bug.

## Why the end columns are the ones that break

The symptom is precise: on a non-startup desktop the columns at the head and
tail of the strip do not travel, while every column between them animates.

A middle column rides `viewDelta` in the geometry batch and is re-derived on
every scroll, so it self-corrects the instant the next batch lands. A parked end
column is different by construction. It is committed off the union of all
outputs (`engine_apply.cpp:401`, `parkTop = unionBottom + 1 + kParkMargin`)
because, as the wire documents, a rect is the only clip every present path
honours. It is then denied the view delta outright:

```cpp
// engine_apply.cpp:641
if (!parkedNow && viewDelta != 0) {
    obj[QLatin1String("viewDelta")] = viewDelta;
```

so its entire on-screen existence during a slide comes from two pieces of
**retained** state combined in `scrollParkedOffscreen()` (`scroll_clip.cpp:140`):
its `m_scrollVisualDelta` entry, and `StripViewAnimator::offsetFor(output)`.
Both are strip-scoped in meaning and neither is strip-keyed in fact.

Parked columns are the load-bearing case for the one thing the seam cannot
express. That is why they fail first and fail alone.

## What actually breaks: three live candidates

Source reading cannot separate these. All three produce "ends teleport, middles
animate, only off the startup desktop".

**A. No batch is emitted, so nothing repairs the stale view.**
`engine_apply.cpp:746` is emit-on-change:

```cpp
// Emit-on-change: a relayout that resolved every window to the exact
// rect already applied ... must not re-feed the compositor's apply path.
if (anyEntryChanged) {
    Q_EMIT windowsTiled(...);
}
```

The batch is built full-strip (`arr.append(obj)` for every tile) but emitted
only if some rect changed. Returning to a desktop whose strip is unchanged
emits nothing at all. Any repair carried *on a batch* is therefore inert in
exactly the failing case. This is the finding that killed the previous draft of
this plan, which put strip identity in `TileRequestEntry`.

**B. The animator's view offset belongs to the other strip.**
`m_motions` is keyed by `LogicalOutput*`. Its header explains the choice:

> The coordinate's ORIGIN is arbitrary... It deliberately does not try to mirror
> the engine's own `viewOffset`: the two would have to be kept in step across
> every context switch, screen change and restore, and nothing needs them to
> agree.

The paint offset is `committed - animated`, and that difference is only
meaningful if both terms accumulated from the same strip. The reasoning is sound
within one strip and does not survive a context switch. Note the offset rings
out to zero when settled, so this candidate predicts corruption *during* a
slide, not at rest.

**C. `m_scrollVisualDelta` is absent for the incoming strip's parked columns.**
Inserted only at `tiling.cpp:1638`, under `snap.hasVisualPos`, and removed at
ten sites. If a removal fires on the switch, or an entry was simply never
established for the incoming strip, `scrollParkedOffscreen` returns false and
the column paints at its committed rect, off screen.

They are not mutually exclusive and A can mask the others.

## Gate before any code

One `qCDebug` in `scrollParkedOffscreen` (`scroll_clip.cpp:140`) reporting, per
parked window: whether the `m_scrollVisualDelta` lookup hit, the placement it
returned, and what `offsetFor(managed)` gave. Plus one line at
`slotWindowsTileRequested` (`tiling.cpp:66`) recording that a batch arrived at
all.

Predictions, on the live desktop-2 repro:

- **A** — no batch line on the desktop switch, and none until the first scroll.
- **B** — lookup hits with a correct placement, offset is non-zero and belongs
  to the previous strip's travel.
- **C** — lookup misses for the parked columns.

This is a gate, not a footnote. The previous draft of this document prescribed
a fix for a mechanism nobody had confirmed, and both mechanisms it assumed have
since been ruled out.

## Why the compositor must not derive identity itself

Whatever the mechanism, the repair involves naming strips. The obvious spelling
is a `desktop` field the effect compares to `KWin::effects->currentDesktop()`.
That is wrong, and the engine already says why. From `ScrollEngine.h:1622`:

> ONE EXCEPTION to "the context the producer resolved for": a screen under a
> sticky-desktop pin. `currentKeyForScreen` answers with the **PINNED** desktop
> (the pin outranks the per-output desktop), while the daemon resolves its
> template against the **LIVE** one and knows nothing about the pin. It is
> engine-internal state with no accessor.

The engine's strip identity is deliberately not always the live compositor
desktop. An effect comparing against KWin's current desktop is permanently wrong
for every pinned screen, needs a second arm for activities, and a third for
all-desktops windows. `#808` is that shape already.

**The engine is the sole authority on strip identity. The compositor compares
it and never computes it.** An opaque token makes the mistake unrepresentable.

One concession: opacity costs log readability, and this subsystem is diagnosed
from `journalctl`. The token travels with a **debug-only human label**
(`"LG…|2|"`) that is logged and never branched on. If that label is ever read by
logic, the design has failed.

## Stage 1: strip identity as an announcement

**Identity is a fact about a screen, not a rider on placement.** Given
candidate A, it cannot live in `TileRequestEntry`.

**Wire.** A new signal on the Scrolling interface:

```xml
<signal name="stripContextChanged">
  <arg name="screenId" type="s"/>
  <arg name="epoch" type="s"/>   <!-- opaque; hash of the engine's context key -->
  <arg name="debugLabel" type="s"/> <!-- logging only, never branched on -->
</signal>
```

Emitted whenever a screen's resolved context key changes, independent of
whether any geometry changed. Fires on desktop switch, activity switch, pin
acquire and release. Adding a signal does not break the existing
`windowsTileRequested` signature, so no lockstep wire break and no stale-`.so`
hazard, which matters given the effect cannot be hot-reloaded (unload/loadEffect
does not reload; it needs a logout).

**Consumer.** The effect holds `QHash<QString /*screenId*/, QString /*epoch*/>`.
On a changed epoch it retires the strip-scoped state for that screen, before any
subsequent batch applies.

**What is retired, and why each.** The previous draft listed this set by guess.
Corrected against each member's own documentation:

| State | Retire? | Reason |
| --- | --- | --- |
| `m_scrollVisualDelta` (windows on that screen) | **yes** | describes a position on *this* strip; meaningless on another |
| `StripViewAnimator` motion for the output | **yes** | see below; a cross-strip offset is the candidate-B corruption |
| `m_scrollCommandedRects` | **no** | client-negotiation state, not paint state. A rate-limited counter-assert against a client refusing geometry. Retiring it mid-flight disarms a defence for no benefit |
| `m_scrollOfferedColumn` | **no** | size-continuity discriminator. Its doc: "an entry means the client HAS been offered this column". Dropping it makes the next batch re-offer and can resize a settled window |
| screen-keyed sets (`m_scrollingScreens`, crop, vertical axis) | **no** | daemon re-pushes these on context change; no evidence they are implicated |

The distinction that got the first draft wrong: **strip-scoped paint state** is
about a strip and dies with it; **client-negotiation state** is about a window
and its lifetime is the window's. Only the former is retired.

**Animator re-keying.** `m_motions` moves from `LogicalOutput*` to
`(LogicalOutput*, epoch)`. This is the smallest change that makes candidate B
unrepresentable rather than merely repaired, and it follows directly from the
header's own admission that it declines to track context. A motion for a
retired epoch is dropped, not carried.

**Multi-epoch batches.** A batch already spans screens, and a window migrating
desktops may put two epochs for one screen in one batch. Entries are grouped by
`(screenId, epoch)` and each group is retired-then-applied as a unit. Applying a
batch whole and retiring once is order-dependent and would discard state
established by an earlier entry in the same batch.

**Kill switch.** Behind a config gate for at least one release. A behaviour
change in the compositor with no runtime disable costs every user a logout to
back out.

**Test.** A context change retires strip-scoped paint state and leaves
client-negotiation state intact. Neither assertion exists today, and the first
is what every previous fix slipped past.

## Stage 2: directory, generation, subscription

Needed the moment the effect renders more than one strip. Independently useful
before that: generation numbers make staleness detectable rather than merely
survivable.

**Directory.** With one strip the effect only compares tokens. With several it
must choose among them, and an overview must order them. An opaque token cannot
be enumerated or sorted by the compositor, and ordering by desktop index
reintroduces the pinned-desktop divergence. So the engine publishes, per output,
an ordered list of `(epoch, displayOrder, label)`. The effect selects from the
published list and still never computes identity.

**Generation.** A token detects replacement, not drift. The daemon can reflow a
strip while it is not current. Each epoch carries a monotonic generation; a bump
means the cache for that epoch is stale though its identity is unchanged. This
also subsumes candidate A properly: a generation bump is emitted on a resolve
that changed nothing geometrically but changed which strip is current.

**Subscription.** The effect declares which epochs it needs, normally one and
several during an overview. The daemon emits batches for exactly that set.
Push-always is the wrong default: N times the batch traffic at scroll rates for
strips nobody is looking at, and the drag auto-scroll heartbeat already runs
these at roughly 60 Hz.

**Known race.** The effect learns of a context switch from KWin, the daemon from
its own path, and they will disagree for at least a frame. That is the same
class as `#728` (desktop-switch focus race). The subscription must be
declarative and idempotent — the effect states what it wants and the daemon
converges — rather than a request/response that can interleave.

## Stage 3: separating commitment from depiction — SKETCH ONLY

Not a design. Recorded so stages 1 and 2 do not foreclose it, and so its
unsolved problem is on the record.

Only one strip per output can own real window geometry. The others are depicted,
scaled and offset, so they cannot commit rects.

The seam has already cracked here. The park rect exists because "a rect is the
only clip every present path honours"; `visualPos` exists because "the park is
not where the column IS on the strip". That pair is a strip-space coordinate
bolted onto a wire that speaks only screen-absolute rects. Finishing the split
would make strip-space position primary and commitment a per-output exception.

**The unsolved problem.** The park hack exists *because* rects are the only
universal clip. If entries stop committing rects, nothing in the present
pipeline stops a depicted strip painting onto a neighbouring monitor. Stage 3
requires a clip mechanism that does not rely on committed geometry, and this
document does not have one. Do not treat stage 3 as costed.

## Sequencing

| Stage | Buys | Wire change | Do it |
| --- | --- | --- | --- |
| 0 Instrumentation | Identifies which candidate is real | none | First, always |
| 1 Identity announcement | Fixes the bug for all three candidates | one added signal | After stage 0 |
| 2 Directory + generation + subscription | Coherence; N-strip capable | more signals | Next |
| 3 Coordinate split | Removes the park special case | reshapes the batch | Only with overview, and only once the clip problem is solved |

Stages 1 and 2 are worth doing whether or not an overview lands. Stage 3 is
gated on both overview and an answer to the clip problem; the v3.5
dynamic-workspaces work currently has overview descoped.

## Non-goals

- Teaching the compositor what a virtual desktop is. It compares tokens.
- Retiring the ten ad-hoc `m_scrollVisualDelta` removal sites in stage 1. Once
  the invariant holds they become provably redundant and can go in a separate
  pass, with the epoch retirement as the argument. Mixing "establish the rule"
  with "collect the winnings" makes a regression impossible to bisect.
- Re-keying `m_lastAppliedRect`. Real, same class, not this bug; separate change.

## Verification

The defect reproduces on a second virtual desktop with enough columns that head
and tail park: scroll to either end and the terminal column teleports instead of
travelling, while middles animate.

Build and `ctest` are not evidence here. The claim is about compositor paint
behaviour across a context switch, so it is verified live, on the repro, with
the stage 0 instrumentation still in place, before and after.
