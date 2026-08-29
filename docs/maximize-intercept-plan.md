<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Maximize interception → per-mode maximize

Intercept a window's maximize request (titlebar button, Meta+PgUp,
client-side request) once, in the effect, and dispatch it to the placement
mode resolved for that window's screen. Each mode answers "maximize" in its
own terms. The scrolling arm routes to `toggleMaximizeColumn`, the same verb
Meta+Alt+F drives.

This is a cross-cutting feature, so per CLAUDE.md all three modes need an arm
before it is done. Only scrolling has a verb to call today (see Per-mode
dispatch); the other two arms are design work, not plumbing.

## Premise

Maximize is internal engine state, held per mode. For scrolling that state is
`ScrollStrip::m_preMaximizeWidth` + `m_preMaximizeColumnIdx`
(`ScrollStrip.h:741`), a single focused-column slot whose index is fixed up on
every insert / move / remove in `scrollstrip_structure.cpp`. The scrolling
engine never sets KWin's own `MaximizeFull` bit, and column geometry is
engine-authoritative: the next apply batch overwrites whatever KWin wrote.

So this is not "undo a maximize and redirect it". It is a two-way sync between
KWin's maximize bit and the strip's maximize slot, with the engine as the
authority on both directions.

## Existing precedent

`clearWindowedFullscreen` is the same shape and should be copied rather than
reinvented:

- effect → daemon: reply-gated `asyncCall` with an in-flight marker
  (`windowedfullscreen.cpp`, `dispatchWindowedFullscreenClear`)
- daemon → effect: the authoritative echo rides the **apply batch entry**, not
  a bespoke signal (`engine_apply.cpp`, the `windowedFullscreen` key)
- the decision chain lives in a header-only, unit-testable pure module
  (`kwin-effect/tilinghandler/scrolldecisions.h`), with KWin-facing side
  effects kept at the call site in `tiling.cpp`

kwin-effect has no linkable test target, so pure decision logic MUST go in
`scrolldecisions.h` to be covered at all.

## Work

### 1. Inbound: KWin maximize → engine verb

**Hook.** `KWin::EffectWindow::windowMaximizedStateChanged`, already connected
at `window_connections.cpp:768` for the maximize shader morph. Add an arm
there, after its per-axis edge filter; do not add a second connect to the same
signal.

The neighbouring `windowMaximizedStateAboutToChange` (connected at
`window_connections.cpp:713`) is the morph's departure-rect capture and is NOT
the hook: it fires pre-commit, so the cancel would race the very state change
it is trying to replace. The post-commit signal lets the interception read
what actually landed and write the engine's answer over it.

**Dispatch, not gate.** Resolve the window's engine-authoritative screen and
its mode, then hand off to that mode's arm (see Per-mode dispatch). The effect
already has the discriminator it needs:
`TilingHandler::isScrollingScreen(screenId)` (`tilinghandler.h`, the
`m_scrollingScreens & m_managedScreens` intersection), alongside
`m_managedScreens` for the mode resolution generally. Note the transient
window called out at `tilinghandler.h` — `m_scrollingScreens` can name a
screen the managed set has not caught up to, which is why the intersection
exists and why the raw set must not be read directly.

**Floated** windows are per-mode non-members by design and keep stock
maximize, in every mode. That is the wanted behaviour and is the one genuine
pass-through.

**Unmanaged screens** pass through untouched.

**Axis edge filter.** The signal fires once per axis: a half-snapped window
going to full fires twice (vertical-only, then full). A toggle verb fired
twice cancels itself. Reuse the fully-maximized edge tracking already at
`window_connections.cpp:768` (`m_lastFullyMaximized`); only a transition into
or out of `MaximizeFull` drives the verb.

**KWin bit.** Geometry needs no rescue, but the maximize-mode bit does: left
set it desyncs the titlebar button and fights later frame changes. Clear it
under the existing `m_suppressMaximizeChanged` guard (`signals.cpp`) so
`slotWindowMaximizedStateChanged` does not read our own clear as a manual
unmaximize.

**Transport.** `ScrollingAdaptor` has no `toggleMaximizeColumn` — the verb is
shortcut-only today, `ShortcutManager::scrollMaximizeColumnRequested` →
`ScrollEngine::toggleMaximizeColumn` (`src/daemon/daemon/scrolling_init.cpp:162`,
`engine_verbs.cpp`). Add:

- method in `dbus/org.plasmazones.Scrolling.xml`
- slot on `ScrollingAdaptor`, forwarding to the same `ScrollEngine` entry
  point the shortcut uses, with the same wire-boundary input validation as
  `clearWindowedFullscreen` (`scrollingadaptor.cpp`)

Screen AND window scoped, unlike every other scrolling verb. The others are
driven by a shortcut, which means "act on what I am looking at", so the active
column is the right target and the screen is enough. This one is driven by a
maximize request naming ONE window, and that window is frequently not the
active one — a client maximizing itself from the background, a titlebar click
that does not raise first. Screen-scoped, the verb cancelled the clicked
window's maximize and then resized a different column. The window id is
therefore on the wire, and an empty id keeps the shortcut's "active column"
meaning.

### 2. Outbound: engine state → KWin maximize bit

Without this, Meta+Alt+F leaves the titlebar button rendering un-toggled, and
the two entry points disagree about the same state.

**Predicate.** There is no public "is the active column maximized" getter, and
the honest answer is not a bare boolean. `toggleMaximizeColumnAt`
(`scrollstrip_sizing.cpp:217`) has a fallback branch for a column at full
width with **no** stored slot (maximized in an earlier session, or another
column's maximize discarded the single slot). Decide and write down which
definition the echo carries:

- `m_preMaximizeColumnIdx == idx && width == full` — strict, "we own it"
- renders at full main extent — matches what the user sees

Recommend the second: the button should reflect the window, and the fallback
branch exists precisely so a full-width column always has a way back out.

**Wire.** A per-entry key on the apply batch beside `windowedFullscreen`
(`engine_apply.cpp`). Same ungated-by-presentation reasoning applies —
read that comment before gating on parked/hidden.

**Effect side.** Apply the bit under `m_suppressMaximizeChanged` so the echo
cannot bounce back through the inbound arm as a fresh user maximize.

### 3. Per-mode dispatch

The interception, the axis edge filter, the suppression guard and the pure
decision module are shared. What differs per mode is only the verb the arm
calls and the state the echo reads.

**Scrolling — implemented now.** `toggleMaximizeColumn`
(`engine_verbs.cpp`), state in `m_preMaximizeWidth` /
`m_preMaximizeColumnIdx`. Everything in sections 1, 2 and 4 describes this
arm.

**Tiling — no verb exists.** Monocle is an algorithm output, not a user verb:
there is no `toggle_monocle` in `shortcutmanager_ids.h`, and the KWin maximize
on a monocle tile is the tile engine's own ownership ledger
(`m_monocleMaximizedWindows`, `windowedfullscreen.cpp`) with its own restore
path in `unmaximizeMonocleWindow`. Two candidate answers, both needing a
decision before code:

- promote the focused window to monocle for that screen (needs a new verb in
  the tile engine, and a rule for what happens when the running algorithm is
  not monocle-capable)
- treat it as the existing float-and-maximize, leaving the tiling layout alone

Whichever is chosen, the arm MUST route through the monocle ledger rather than
setting the KWin bit beside it, or it will fight `unmaximizeMonocleWindow`.

**Snapping — no verb exists.** Nearest existing behaviour is
`kIdRestoreWindowSize` and the zone-snap family. The natural answer is "snap
to the layout's largest zone, toggling back to the previous zone", which
implies a pre-maximize slot per screen mirroring the strip's — snapping has no
such state today.

The float-is-per-mode invariant applies throughout: one mode's maximize slot
never gates another's, exactly as its float slot does not
(`WindowPlacement.h`).

### 4. Decision logic

`resolveColumnMaximizeAction` in `scrolldecisions.h`, sibling to
`resolveWindowedFullscreenAction`, over THREE inputs: flag on wire,
effect-side membership, and KWin's maximize state. Pure; the call site in
`tiling.cpp` acts on the verdict. Unit-tested by including the header.

No in-flight marker, unlike the windowed-fullscreen decision — the Status
section below records why the round trip cannot be raced. The KWin input is
`requestedMaximizeMode()` rather than the committed `maximizeMode()` **on the
batch arm** (the interception passes the committed mode instead, because there
it compares against what actually landed), for the
lag reason the windowed-fullscreen arm documents at its own call site.

## Open decisions

1. ~~Setting or unconditional?~~ **Settled: unconditional.** No config key,
   no gating. On a managed screen the maximize button means the mode's
   maximize.
2. ~~Echo predicate?~~ **Settled: renders at full main extent**, not the
   strict "we own the slot" test. The button reflects what the user sees, and
   `toggleMaximizeColumnAt`'s no-stored-slot fallback exists precisely so
   a full-width column always has a way back out.
3. **Tiling's answer** — promote to monocle, or float-and-maximize?
4. **Snapping's answer** — largest-zone toggle, and where its pre-maximize
   slot lives.

## Out of scope

Client-requested **fullscreen** is a separate signal chain with its own
ownership ledger (`m_windowedFullscreenWindows`) and is untouched.

---

## Status: seam + scrolling arm BUILT

Verified on a live compositor (nested `kwin_wayland --virtual`, build-tree
effect and daemon) — see Verified below for what was actually driven and what
was not. Build and test state is deliberately not recorded here: it changes
with every commit, so a checked-in claim about it is stale the moment it is
written.

### What landed

**Wire (v5 → v6).** `Scrolling.toggleMaximizeColumn` gained a `windowId`
argument, `(s)` → `(ss)`, so the verb acts on the column holding the named
window rather than on whichever column happens to be active. An empty id keeps
the shortcut's "active column" meaning. A method signature change breaks a
mismatched peer exactly as a widened struct does, so it takes the same version
bump.

**Wire (v5 → v6).** `TileRequestEntry` gained `columnMaximized`, inserted after
`windowedFullscreen`: `a(siiiissbbbssiiibsb)` → `a(siiiissbbbbssiiibsb)`.
Carried as data rather than inferred effect-side from the committed rect,
because a tile's main extent is the column's LESS any tab-indicator
reservation, so a maximized tabbed column would measure under full width. Same
doctrine as `tabFrom`. Touches `ServiceConstants.h` (v6 section + both version
constants), `AutotileTypes.h`, `marshalling.cpp`, `types.cpp` (two rejected
pairs, floating and monocle; the windowedFullscreen pair is explicitly LEGAL),
both XML contracts, and the daemon's JSON parse.

**Engine.** `engine_apply.cpp` measures the RESOLVED column against the work
area's main extent and emits the flag on every tile of the column, ungated by
presentation. Deliberately not read off `m_preMaximizeColumnIdx`: that slot
answers "is there a stored width to go back to", which is a different question
from "is this column maximized" (see `toggleMaximizeColumnAt`'s
no-stored-slot arm). Degenerate work area guarded.

A column PINNED BY ITS MINIMUM is excluded, via `ResolvedColumn::
extentPinnedByMinimum`. Its extent comes from its tiles' declared minimum
rather than from any width the user chose, so when that floor alone reaches
the work area the column renders full width whatever the intent says — and
measured off the rect it would report maximized permanently, latching the
titlebar button with no way to un-latch it. The verb refuses the same class of
column for the matching reason: neither arm of the toggle can make it
narrower, so reporting success would raise a resize OSD for a press that moved
nothing.

**Verb entry test.** Every full-width comparison in `toggleMaximizeColumnAt`
is in RESOLVED PIXELS, never on the `ColumnWidth` value. `operator==` compares
kind first, so `Fixed(<work area main>)`, `Preset(1.0)` and `Proportion(1.0)`
render identically and only one is `== full`. Since the batch MEASURES the
rendered column, every other route to a full-width column (a width verb
clamping, preset cycling, expand, equalize, an edge-drag reconcile, a
cross-screen handoff, expel, the minimize round trip, a restore from disk)
lights the button up — and on a kind compare each of those took the maximize
arm, rewrote intent, rendered the same pixels and reported success. The stored
slot is re-validated against the CURRENT work area for the same reason: a
`Fixed` width captured on a wider area resolves clamped back to full.

**Inbound.** `ScrollingAdaptor::toggleMaximizeColumn`, forwarding to the same
`ScrollEngine::toggleMaximizeColumn` the shortcut uses.
`TilingHandler::interceptMaximizeRequest` gates on tiled membership (float
passes through) and `isScrollingScreen`, CANCELS KWin's flip back to engine
state, and dispatches the toggle. Hooked into the existing
`windowMaximizedStateChanged` lambda after its edge filter, so per-axis double
firing cannot cancel the toggle against itself.

**Outbound.** `m_columnMaximizedWindows`, an ownership ledger on the
`m_monocleMaximizedWindows` pattern, driven from the batch by a pure 3-way
`resolveColumnMaximizeAction`.

Released on EVERY path that ends the strip's claim, which is more than the
teardowns: close and cross-output transfer (`cleanupAutotileTracking`), the
batch's own Release arm, engine disable, daemon loss, daemon bring-up, effect
unload, BOTH float channels (`applyFloatCleanup` and `applyPassiveFloatShed`),
the `onComplete` untile diff, the demote and removed-screen sweeps in
`screenschanged.cpp`, the no-strip-left arm of `handleWindowOutputChanged`, the
leaving-scrolling loop in `setScrollingScreens`, and the
fullscreen-exit-while-floating repair in `slotWindowFullScreenChanged`.

The batch is NOT a release path for a window that LEAVES the strip: no entry
arrives to carry a cleared flag, so each of those exits must release for
itself. The scroll↔scroll handoff deliberately does not — the destination strip
owns the bit and answers on its own first batch.

`releaseColumnMaximized` RETAINS membership when it skips a still-fullscreen
window, matching `unmaximizeMonocleWindow`. Shedding it there stranded the bit:
the Apply arm cannot re-assert on any path that reaches the release, because
Apply requires the wire flag and every such path has it false.

**No in-flight marker**, unlike `clearWindowedFullscreen`. The interception
changes no membership of its own, so effect state still agrees with the
pre-toggle flag during the round trip and a stale batch resolves to None on its
own terms. A toggle is not idempotent, so a marker could not be applied
symmetrically anyway. Both directions are pinned in
`staleBatchDuringToggleIsInert`.

**Idempotence against our own echo.** `m_suppressMaximizeChanged` covers the
synchronous X11 emission from inside `maximize()`, but the Wayland committed
signal arrives a round trip later with the counter back at 0. The
already-agrees arm at the top of `interceptMaximizeRequest` is what stops that
echo dispatching a second toggle.

**Tests.** 8-row truth table in `test_scroll_decisions`, plus a stale-batch
walk that THREADS membership through the sequence rather than hand-writing each
triple — written as independent calls it only restated rows of the table and
could not fail on its own. A two-tile-column emit test in
`test_scrollengine_smoke` pinning that the flag rides EVERY tile, does not leak
onto a sibling column, and clears on the way back; and a targeting test pinning
that the verb aims at the named window's column, refuses an unknown window, and
still means "active column" for an empty id. Two rows in
`test_scrollstrip_sizing` for the rendered-vs-intent entry test and the stale
restore slot. Gate-chain coverage for the new verb in
`test_scrolling_adaptor_verbs` and the cleared-engine sweep. The JSON key
spelling is pinned in `test_tiling_adaptor_panel_gate`, which is the only place
that would catch a producer/consumer rename. `validationError`'s two new
rejections and the legal `windowedFullscreen` pairing are pinned in
`test_phosphorprotocol`. Wire fixtures compare the new field in both
directions, and the version pin is updated.

### The anti-ballooning exemption

The batch's anti-ballooning clear (`tiling.cpp`, the `MaximizeRestore` call on
every non-monocle tiled entry) now EXEMPTS a `columnMaximized` entry, or the
bit would be stripped every batch before anything could see it.

The exemption is required — without it the bit is set and stripped within one
lambda — and the STEADY state is precedented rather than novel. The monocle arm
directly below it already holds a window at `MaximizeFull` against a gapped,
non-maximize-area rect and re-asserts it every batch, and has shipped that way.
What protects both is that KWin's maximize-area re-assert is event-driven
rather than continuous, and that the geometry apply lands last in the same call
stack inside the same `inGeometryApply` guard.

The counter-assert is NOT part of that cover, contrary to an earlier reading
here: it is gated on `!isWaylandClient()`, so it does not exist for a Wayland
column at all, and it is rate-capped besides.

The sharpest case is the WORK-AREA CHANGE, and it was measured rather than
argued. On such a change KWin re-maximizes every `MaximizeFull` window. A
monocle window differs from the maximize area by a few gap pixels; a tile in a
MULTI-TILE column differs by its whole cross extent, so a re-assert there would
put every tile full screen over its siblings.

Driven in the nested harness on a two-tile maximized column — both tiles at the
full main extent, each holding half the cross extent, both carrying
`MaximizeFull`, which is the hazard state exactly — across a real
logical-geometry change from 1600x900 to 1280x720. Both tiles held their
stacked column rects (full main extent, half cross extent each) and neither
took KWin's maximize area. Reversing the change restored the original rects.
The engine's apply lands last and wins.

### Verified live

In a nested `kwin_wayland --virtual` session running the build-tree effect and
daemon:

- the v6 handshake gate refuses a mismatched peer, with the installed
  daemon rejected by version before the build-tree pair registered at 7
- a maximize aimed at a NON-focused window grows that window's column and
  leaves the focused column untouched, which is the whole point of the
  `windowId` argument
- the flag rides every tile of a two-tile column and clears on the way back,
  and the un-maximize returns the column's PRE-MAXIMIZE width rather than the
  default, so the stored slot resolves
- sixteen consecutive compositor-driven maximize/restore edges converged in
  both directions, with no echo ping-pong
- the exemption does not balloon, single-tile or multi-tile (above)

NOT covered by that session, and therefore resting on review alone: every
release path that ends strip membership (both float channels, the untile diff,
cross-output transfer, the leaving-scrolling loop, the two screen sweeps, the
fullscreen-exit repair). The harness ran one output, one client class, no mode
flips and no cross-output moves. None of those paths is reachable by a unit
test either, because there is no linkable kwin-effect test target — which is
the single highest-leverage gap in this subsystem's coverage.

### Still open

Tiling and snapping arms, both blocked on their design decisions above.
