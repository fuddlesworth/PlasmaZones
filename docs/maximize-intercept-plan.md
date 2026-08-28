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
(`ScrollStrip.h:681`), a single focused-column slot whose index is fixed up on
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
  (`windowedfullscreen.cpp:244`, `dispatchWindowedFullscreenClear`)
- daemon → effect: the authoritative echo rides the **apply batch entry**, not
  a bespoke signal (`engine_apply.cpp:553`, the `windowedFullscreen` key)
- the decision chain lives in a header-only, unit-testable pure module
  (`kwin-effect/tilinghandler/scrolldecisions.h`), with KWin-facing side
  effects kept at the call site in `tiling.cpp`

kwin-effect has no linkable test target, so pure decision logic MUST go in
`scrolldecisions.h` to be covered at all.

## Work

### 1. Inbound: KWin maximize → engine verb

**Hook.** `KWin::EffectWindow::windowMaximizedStateAboutToChange`, already
connected at `window_connections.cpp:743` for the morph departure-rect
capture. Add an arm; do not add a second connect to the same signal.

**Dispatch, not gate.** Resolve the window's engine-authoritative screen and
its mode, then hand off to that mode's arm (see Per-mode dispatch). The effect
already has the discriminator it needs:
`TilingHandler::isScrollingScreen(screenId)` (`tilinghandler.h:628`, the
`m_scrollingScreens & m_managedScreens` intersection), alongside
`m_managedScreens` for the mode resolution generally. Note the transient
window called out at `tilinghandler.h:619` — `m_scrollingScreens` can name a
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
under the existing `m_suppressMaximizeChanged` guard (`signals.cpp:185`) so
`slotWindowMaximizedStateChanged` does not read our own clear as a manual
unmaximize.

**Transport.** `ScrollingAdaptor` has no `toggleMaximizeColumn` — the verb is
shortcut-only today, `ShortcutManager::scrollMaximizeColumnRequested` →
`ScrollEngine::toggleMaximizeColumn` (`scrolling_init.cpp:163`,
`engine_verbs.cpp:145`). Add:

- method in `dbus/org.plasmazones.Scrolling.xml`
- slot on `ScrollingAdaptor`, forwarding to the same `ScrollEngine` entry
  point the shortcut uses, with the same wire-boundary input validation as
  `clearWindowedFullscreen` (`scrollingadaptor.cpp:301`)

Screen-scoped like every other scrolling verb, so the effect passes the
window's resolved screen, not the window id.

### 2. Outbound: engine state → KWin maximize bit

Without this, Meta+Alt+F leaves the titlebar button rendering un-toggled, and
the two entry points disagree about the same state.

**Predicate.** There is no public "is the active column maximized" getter, and
the honest answer is not a bare boolean. `toggleMaximizeActiveColumn`
(`scrollstrip_sizing.cpp:171`) has a fallback branch for a column at full
width with **no** stored slot (maximized in an earlier session, or another
column's maximize discarded the single slot). Decide and write down which
definition the echo carries:

- `m_preMaximizeColumnIdx == idx && width == full` — strict, "we own it"
- renders at full main extent — matches what the user sees

Recommend the second: the button should reflect the window, and the fallback
branch exists precisely so a full-width column always has a way back out.

**Wire.** A per-entry key on the apply batch beside `windowedFullscreen`
(`engine_apply.cpp:553`). Same ungated-by-presentation reasoning applies —
read that comment before gating on parked/hidden.

**Effect side.** Apply the bit under `m_suppressMaximizeChanged` so the echo
cannot bounce back through the inbound arm as a fresh user maximize.

### 3. Per-mode dispatch

The interception, the axis edge filter, the suppression guard and the pure
decision module are shared. What differs per mode is only the verb the arm
calls and the state the echo reads.

**Scrolling — implemented now.** `toggleMaximizeColumn`
(`engine_verbs.cpp:145`), state in `m_preMaximizeWidth` /
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

`resolveMaximizeAction` in `scrolldecisions.h`, sibling to
`resolveWindowedFullscreenAction`, over: flag on wire, effect-side membership,
KWin's live `maximizeMode()`, and the in-flight marker. Pure; the call site in
`tiling.cpp` acts on the verdict. Unit-tested by including the header.

## Open decisions

1. ~~Setting or unconditional?~~ **Settled: unconditional.** No config key,
   no gating. On a managed screen the maximize button means the mode's
   maximize.
2. ~~Echo predicate?~~ **Settled: renders at full main extent**, not the
   strict "we own the slot" test. The button reflects what the user sees, and
   `toggleMaximizeActiveColumn`'s no-stored-slot fallback exists precisely so
   a full-width column always has a way back out.
3. **Tiling's answer** — promote to monocle, or float-and-maximize?
4. **Snapping's answer** — largest-zone toggle, and where its pre-maximize
   slot lives.

## Out of scope

Client-requested **fullscreen** is a separate signal chain with its own
ownership ledger (`m_windowedFullscreenWindows`) and is untouched.

---

## Status: seam + scrolling arm BUILT

Built, compiling clean with no new warnings, full ctest green. NOT yet verified
live on a compositor, and one part of it specifically wants that (see Risk).

### What landed

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
from "is this column maximized" (see `toggleMaximizeActiveColumn`'s
no-stored-slot arm). Degenerate work area guarded.

**Inbound.** `ScrollingAdaptor::toggleMaximizeColumn`, forwarding to the same
`ScrollEngine::toggleMaximizeColumn` the shortcut uses.
`TilingHandler::interceptMaximizeRequest` gates on tiled membership (float
passes through) and `isScrollingScreen`, CANCELS KWin's flip back to engine
state, and dispatches the toggle. Hooked into the existing
`windowMaximizedStateChanged` lambda after its edge filter, so per-axis double
firing cannot cancel the toggle against itself.

**Outbound.** `m_columnMaximizedWindows`, an ownership ledger on the
`m_monocleMaximizedWindows` pattern, driven from the batch by a pure 3-way
`resolveColumnMaximizeAction`. Released on close, cross-output transfer, engine
disable, daemon loss, daemon bring-up and effect unload.

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

**Tests.** 8-row truth table plus the stale-batch walk in
`test_scroll_decisions`; a two-tile-column emit test in
`test_scrollengine_smoke` pinning that the flag rides EVERY tile and clears on
the way back. Wire fixtures and the version pin updated.

### Risk to verify live

The batch's anti-ballooning clear (`tiling.cpp`, the `MaximizeRestore` call on
every non-monocle tiled entry) now EXEMPTS a `columnMaximized` entry, or the
bit would be stripped every batch before anything could see it.

That exemption is not fully argued away. KWin re-asserts maximize-area geometry
for a `MaximizeFull` window, and a maximized column is the work area's full
MAIN extent but keeps its gaps and, in a multi-tile column, only a share of the
cross extent. The two authorities compounding is exactly the ballooning that
clear exists to stop. The mitigations in place are the ones the monocle arm
relies on: the bit is set before the geometry apply, in the same call stack,
inside the same `inGeometryApply` guard, and the strip's counter-assert covers
an external mover.

**Check on a multi-tile maximized column, and on a column with gaps
configured.** If it balloons, the fallback is to drop the outbound mirror and
keep the inbound interception, at the cost of the titlebar button not latching.

### Still open

Tiling and snapping arms, both blocked on their design decisions above.
