<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# One funnel for the effect's compositor-state claims

**Status.** The claim vocabulary, the release table and its tests have landed,
and so has `releaseAllClaims` plus three of the sixteen exit paths. The rest of
the matrix below still holds its scattered calls. Everything from "Evidence"
down describes the tree as it stood before the funnel unless a section says
otherwise.

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

Three ledgers, released from **26** call sites. That count and the table below
are the **pre-funnel** measurement, taken before `releaseAllClaims` existed.
They are the evidence for the funnel rather than a description of the tree
after it, so do not read them as current: the funnel replaced most of these
sites with one call carrying a `ClaimScope`, and the scope table in
`scrolldecisions.h` is the authority on what releases where today.

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
void restoreAllClaims(); // NOT IMPLEMENTED: the teardown half of this
                         // proposal never landed. The bulk restores
                         // (restoreAllWindowedFullscreen and friends) are
                         // per-set and cannot be expressed as a per-window
                         // funnel call, so teardown still calls them directly.
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

That constraint is harder than it looks, and it is what shaped how much of the
migration this branch carries. See below.

## What the implementation found

A first attempt wrote the funnel and the site migration, ran into two problems,
and was reverted. The second attempt landed the vocabulary, the funnel, and the
three sites whose ordering could be settled by reading them:
`applyFloatCleanup`, `applyPassiveFloatShed` and `cleanupAutotileTracking`. The
remaining thirteen rows of the matrix still hold their scattered calls, for the
reasons below.

**The table and the tree disagree, and the table is the honest one.** Under a
scope-driven funnel, `cleanupAutotileTracking` releases every claim that
answers to `StripExit` — including monocle, which that path does NOT release
today (it rides `cleanupClosedWindowState`'s bare scrub instead). So the very
first migrated site silently changed shipped monocle behaviour on the close and
cross-output paths. Every blank in the matrix has this property: the funnel
either reproduces it, in which case the scope vocabulary has to grow a case per
historical accident and stops being a simplification, or it fills the blank,
in which case the "no behaviour change" promise is void.

The blanks are not all principled. Some are documented decisions (the passive
channel's monocle exclusion); others are simply where nobody wrote the call.
The refactor cannot tell them apart, and neither can a reader — which is the
original complaint, now aimed at the fix.

**Ordering is per-site, not per-claim.** `applyFloatCleanup` releases windowed
fullscreen early, then does the tiled/floating flips and the relocation-delta
damage, then releases monocle and column, with a comment stating that order is
deliberate ("the monocle unmaximize is a geometry change, and resolving the
chain after it means the resolve sees the window's final shape"). Collapsing
the three into one call forces a position, and either position moves a
compositor write relative to work between them. Several sites have this shape.

**So the sequencing changes.** The funnel is still the right destination, but
it needs a decision per blank cell — keep it and encode the accident, or fill
it and argue the behaviour change — and a per-site ordering audit. Both are
reviewable work; neither is mechanical, and neither is verifiable by the test
suite, because the sites live in the effect. That makes the linkable
kwin-effect test target a genuine PREREQUISITE for the migration rather than a
parallel nice-to-have, which is the opposite of what this document assumed.

What landed first is the vocabulary and the policy: `Claim`, `ClaimScope`,
`claimReleasesOn`, `claimReleaseOrder` and `claimRetainsOnFullscreenSkip` in
`scrolldecisions.h`, with a row per cell in `test_scroll_decisions`. That is
the specification made executable. It changes no behaviour, it pins the
ordering rule whose reversal was a live regression, and it gives the migration
something to be checked against.

`claimReleaseOrder` and `claimRetainsOnFullscreenSkip` are specification rather
than dispatch: the funnel writes its three releases in order and asserts that
order against `claimReleaseOrder` at compile time, and the retention rule is
implemented inside each release body. They are stated here so a future arm
cannot contradict them silently. `ClaimScope::ModeFlip`, `Teardown` and
`FullscreenExitWhileFloating` likewise name rows that have not been migrated
yet; they are pinned by the table's tests so the migration inherits a decided
answer instead of re-deriving one.

### Three things the funnel had to get right

Learned by writing it, reverting it, and writing it again.

**The funnel must report what it released.** Done: `ClaimReleaseResult`.
`applyPassiveFloatShed` gates a
decoration re-resolve on whether the windowed-fullscreen release actually ran
(`shouldDecorateWindow`'s answer changes only for that claim). A `void` funnel
swallows that signal, and gating on "any claim released" instead is a
behaviour change, because the column claim can release where windowed
fullscreen did not. Return a per-claim result.

**`UntrackFunnel` has to be its own scope.** Done, and still unresolved by
design. `cleanupAutotileTracking` serves
both close and cross-output transfer, and does NOT release monocle — that
rides `cleanupClosedWindowState`'s bare scrub. Folding it into `StripExit`
fills that blank silently. Whether the blank is right is genuinely open: it is
correct for a close and questionable for the cross-output half, where the
window survives holding a bit nothing will hand back. Encode it, flag it as
unresolved, and settle it in its own commit.

**Some sites cannot collapse to one call.** Confirmed by the migration.
`applyFloatCleanup` releases
windowed fullscreen early, does the tiled/floating flips and the
relocation-delta damage, then releases the maximize claims, because the
decoration resolve must see the window's final shape. Both positions are
load-bearing, so that site keeps two calls — the funnel is still worth it
there for the claims it does group, but "one call per exit path" is an
aspiration, not an invariant.

### On verifying it

The nested harness CAN drive monocle and column claims: seed Virtual-0 with
`setEngineMode` plus `setTilingAlgorithm: monocle` and both windows come up
holding KWin maximize, and the float / engine-disable / maximize-restore
transitions all run through `kglobalaccel`'s `invokeShortcut`. A
before/after scenario diff is therefore a real regression gate, and
`scripts/nested-kwin` plus a scenario runner is enough to build one.

Two cautions from doing it. The runner must assert its own fixture — window
COUNT included — because `kwrite` is single-instance and two bare launches can
collapse into one window, which then diffs clean against nothing; launch
distinct documents. The second caution has since been fixed in the harness
rather than worked around: a nested run used to leave an installed
`plasmazonesd` D-Bus-activated against the HOST compositor, and once several had
accumulated `daemon.sh` lost the race for the bus name and later runs silently
answered with the host's screens. `run-nested.sh` now shadows the D-Bus service
file so activation starts the build-tree daemon in-session, and `daemon.sh`
reaps daemons whose nested bus is gone.

The windowed-fullscreen claim was NOT reachable this way —
`scroll_toggle_windowed_fullscreen` never engaged in the fixture — so that
third claim still needs either a working fixture or review.

## Sequencing

After #994 merges, not with it. #994 already carries 91 audit findings, a
second wire revision and two merges from main; folding a cross-subsystem
refactor of two shipped subsystems into it would make the diff unreviewable
and unbisectable. The failure mode to avoid is a monocle or windowed-fullscreen
regression that cannot be told apart from the maximize feature.

## Acceptance

Met by what has landed:

- the matrix above reproduced exactly, as a table in code
- scope policy unit-tested in `scrolldecisions.h`'s suite
- every blank cell ruled a decision or an accident, in writing, before the
  funnel fills it (`UntrackFunnel`'s monocle blank is recorded as unresolved
  rather than filled)

Still open for the remaining thirteen rows:

- a linkable kwin-effect test target exists, so the migration is checkable
- every migrated exit path releases through `releaseAllClaims`, and the
  scattered calls on those paths are gone. Not one call per path: a site whose
  claims must land on opposite sides of its own geometry work keeps two, as
  `applyFloatCleanup` does
- monocle and windowed fullscreen behaviour unchanged, checked in the nested
  harness on the paths it can reach (float, maximize/restore, engine disable,
  daemon loss) and by review on the paths it cannot (cross-output transfer,
  mode flip, untile diff)
