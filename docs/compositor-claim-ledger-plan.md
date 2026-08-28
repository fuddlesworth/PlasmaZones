<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# One funnel for the effect's compositor-state claims

The KWin effect imposes compositor state on windows that only it can hand
back: KWin maximize for a monocle tile, KWin fullscreen plus a keep-flag
demotion for windowed fullscreen, and KWin maximize again for a maximized
scroll column. Each is tracked in its own per-window ledger, and each ledger
is released by its own calls scattered across the same set of exit paths.

The scattering is the defect generator. Every exit path has to remember every
ledger independently, and nothing makes an omission visible: a missed release
is an ABSENCE, so it reads as ordinary code and compiles, tests and reviews
clean. What it leaves behind is a window holding compositor state with nothing
recording that the state is owed — stuck maximized or stuck fullscreen, not
recoverable until a teardown.

PR #994 shipped a third ledger and was missing **eight** of its releases.
Fixing those introduced **two** more asymmetries, both caught only by a
confirmation pass. That is the cost of the current shape, measured on one PR.

## Evidence

Three ledgers, released from **26** call sites:

| ledger | release calls |
|---|---|
| `m_monocleMaximizedWindows` | 5 |
| `m_windowedFullscreenWindows` (+ layer snapshots) | 9 |
| `m_columnMaximizedWindows` | 12 |

Those calls do not cover the same paths. Below is every code path that ends
the effect's authority over a window, against what each ledger does there, as
the tree stands **after** #994's fixes. `n/a` means the ledger cannot hold a
window on that path at all (monocle is autotile-only; the scrolling paths
cannot hold a monocle tile).

| # | exit path | monocle | windowed fs | column |
|---|---|---|---|---|
| 1 | `cleanupAutotileTracking` — close, cross-output (`tilinghandler.cpp`) | indirect¹ | ✓ | ✓ |
| 2 | `drainDeadSessionState` — daemon bring-up (`tilinghandler.cpp`) | — | ✓ | ✓ |
| 3 | `applyFloatCleanup` — active float channel (`floatcleanup.cpp`) | ✓ | ✓ | ✓ |
| 4 | `applyPassiveFloatShed` — passive float channel (`floatcleanup.cpp`) | —² | ✓ | ✓ |
| 5 | demote pass, desktop/activity switch (`screenschanged.cpp`) | ✓ | ✓ | ✓ |
| 6 | inline pre-tile restore (`screenschanged.cpp`) | n/a | n/a | ✓ |
| 7 | removed-screens sweep (`screenschanged.cpp`) | ✓ | ✓ | ✓ |
| 8 | deferred pre-tile restore (`screenschanged.cpp`) | n/a | ✓ | ✓ |
| 9 | `setScrollingScreens` leaving-scrolling loop (`state.cpp`) | n/a | ✓ | ✓ |
| 10 | `handleWindowOutputChanged`, no-strip-left arm (`outputchange.cpp`) | — | ✓ | ✓ |
| 11 | `onComplete` untile diff (`tiling.cpp`) | — | ✓ | ✓ |
| 12 | batch arms — Release / non-monocle demotion (`tiling.cpp`) | ✓ | ✓⁴ | ✓ |
| 13 | `slotEnabledChanged` — engine disable (`signals.cpp`) | ✓ | ✓ | ✓ |
| 14 | fullscreen-exit-while-floating repair (`signals.cpp`) | ✓ | n/a³ | ✓ |
| 15 | effect unload (`lifecycle.cpp`) | ✓ | ✓ | ✓ |
| 16 | daemon loss (`lifecycle_wiring_daemon.cpp`) | ✓ | ✓ | ✓ |

¹ monocle rides `cleanupClosedWindowState`'s bare scrub on this path — the
entry is dropped without a maximize restore, which is correct only because a
closing window has nothing to restore. It is not the same mechanism as the
other two, and it does not cover the cross-output half.
² deliberate and documented at the site: re-driving a maximize restore from a
passive float signal has not been shown safe against the monocle batch that
owns that membership.
³ the arm exists precisely because the monocle ledger has the mirror-image
problem; windowed fullscreen is not held on this path.
⁴ through its own 5-way `resolveWindowedFullscreenAction`, not a shared call —
which is the row that shows the funnel is not merely bookkeeping. All three
ledgers already make a per-window decision here; only the decision's *shape*
differs, and only one of the three is pure and unit-tested.

Rows 1, 2, 4, 10 and 11 are where the three columns disagree. Every disagreement
is either a deliberate, documented policy difference or a latent bug, **and
nothing in the code distinguishes the two.** That is the whole problem: the
reader cannot tell an intentional blank from a forgotten one without tracing
each path by hand, which is exactly the audit that found eight blanks.

## What this would have prevented

From PR #994, all found by review or by running the code rather than by any
test:

- eight missing `m_columnMaximizedWindows` releases (rows 3, 4, 7, 9, 10, 11
  plus the two `screenschanged` restore sites)
- `releaseColumnMaximized` shedding membership above its fullscreen guard,
  where the sibling ledger retains
- `restoreAllColumnMaximized` dropping an entry on a resolve miss while
  retaining one on a fullscreen skip — two paths meaning the same thing,
  behaving differently
- two `screenschanged` sites stripping the KWin bit with a bare
  `maximize(MaximizeRestore)` while leaving membership behind
- teardown ordering: the column restore run before the fullscreen release,
  where the skip-and-drop then loses the claim

Under a single funnel, none of these is expressible: there is one call per exit
path, and the per-ledger behaviour is decided in one place.

## Proposed shape

A claim table plus one release funnel. The three ledgers keep their own storage
and their own restore bodies; what unifies is *when* they are consulted.

```cpp
struct CompositorClaim {
    QSet<QString>* members;
    bool retainOnFullscreenSkip;   // monocle: yes. column: yes. windowed fs: no.
    void (TilingHandler::*restore)(const QString&, KWin::EffectWindow*);
};

// One call per exit path, replacing 26 scattered ones:
void releaseAllClaims(const QString& windowId, KWin::EffectWindow* w, ClaimScope scope);
// Teardown, in declared order:
void restoreAllClaims();
```

`ClaimScope` is what keeps the deliberate blanks deliberate. A path declares
what it is (`StripExit`, `PassiveFloat`, `ModeFlip`, `Teardown`), and the table
declares which claims respond to which scope. Row 4's monocle exclusion stops
being an absence a reader has to notice and becomes a cell they can read.

**The refactor must not flatten the per-kind differences.** These are real and
each is load-bearing:

- monocle and column RETAIN membership when they skip a still-fullscreen
  window; windowed fullscreen does not
- `applyPassiveFloatShed` excludes monocle deliberately, and must include
  column
- the scroll↔scroll output handoff must release NOTHING — the destination
  strip owns the bit and answers on its own first batch
- teardown order is windowed fullscreen BEFORE column, so the column restore
  sees a window whose fullscreen has already been cleared on X11
- the batch Release arm is the one path that releases column while the window
  stays strip-managed

Today those live as prose in six files. In a table they are one column each.

## Testability

`kwin-effect` has no linkable test target, which is why none of the eight gaps
had a test. That does not block this work — it argues for it.

The funnel splits cleanly:

- **which claims release for a given scope** is pure policy over the table,
  and belongs in `scrolldecisions.h` with the other pure decisions. It gets a
  truth table like `resolveColumnMaximizeAction`'s, and a new claim kind or a
  new scope gets a row rather than a code review.
- **the compositor calls** stay in the effect and stay untestable, exactly as
  now.

So the half that has been getting this wrong becomes the half that is covered.
That is a strictly better position than today, independent of whether a
linkable effect target ever appears.

## Scope

**In:** the three ledgers above, their release call sites, the claim table, the
pure scope policy and its tests.

**Out:** `m_scrollCommandedRects` and the other per-window bookkeeping maps.
They are not claims — nothing is owed back, and forgetting one strands
nothing. Folding them in would blur the distinction the table exists to make.

**Not a behaviour change.** Every cell in the table should reproduce what the
tree does today, including the deliberate blanks. Any cell that changes
behaviour is a separate, argued commit — the refactor's value is that it makes
such a change reviewable.

## Sequencing

After #994 merges, not with it. #994 already carries 91 audit findings, a
second wire revision and two merges from main; folding a cross-subsystem
refactor of two shipped subsystems into it would make the diff unreviewable
and unbisectable. The failure mode to avoid is a monocle or windowed-fullscreen
regression that cannot be told apart from the maximize feature.

## Acceptance

- one `releaseAllClaims` call per exit path, and the 26 scattered calls gone
- the matrix above reproduced exactly, as a table in code
- scope policy unit-tested in `scrolldecisions.h`'s suite
- monocle and windowed fullscreen behaviour unchanged, checked in the nested
  harness on the paths it can reach (float, maximize/restore, engine disable,
  daemon loss) and by review on the paths it cannot (cross-output transfer,
  mode flip, untile diff)
