# PR #533 audit follow-ups

Items the `/code-audit` pass on PR #533 (`feature/phosphor-settings-ui`) flagged
but explicitly deferred. Each entry names the original audit finding code, the
scope of work required, the rationale for deferring, and the suggested approach
when the follow-up PR lands.

Recorded after audit Pass 1 closed with 210/210 tests passing. None of the
items below are correctness blockers for the merge of PR #533 — they are
architectural debt, behavioural refactors with non-trivial regression risk,
or design decisions the audit could not unilaterally make.

---

## 1. ISettings* across all page controllers (D5 — HIGH)

**Finding:** Six page controllers (`EditorPageController`,
`GeneralPageController`, `SnappingAppearanceController`,
`SnappingBehaviorController`, `TilingAlgorithmController`,
`TilingAppearanceController`, `TilingBehaviorController`) take `Settings*` as
their dependency. `AnimationsPageController` is the lone consumer of
`ISettings*`. CLAUDE.md says:

> Settings access from page controllers must go through `ISettings` virtual
> methods, not the concrete `Settings` class.

**Scope of work:**

- `ISettings` (`src/core/isettings.h`, ~457 lines, ~57 existing pure virtuals)
  needs to grow ~100 additional pure virtual getter+setter pairs to cover the
  editor / general / snapping-appearance / snapping-behavior / tiling-*
  surface those six controllers use.
- Every new virtual needs a matching `override` declaration in `Settings`
  (`src/config/settings.h`). The methods already exist as non-virtual on the
  concrete class — only the `virtual` keyword + `override` are new.
- `tests/unit/helpers/StubSettings.h` (903 lines, 218 overrides) needs a
  matching stub override for every new pure virtual. Without that the
  StubSettings becomes abstract and every test that instantiates it
  fails to compile.
- Constructor parameters on the six controllers change from `Settings*` to
  `ISettings*`. Since `Settings : public ISettings`, the implicit conversion
  Just Works at the call site — but every member access inside the controller
  that touched a non-`ISettings` method has to either use the new virtual or
  be split out.

**Why deferred:** Disproportionate blast radius for an audit pass. The work
is mechanical but produces a 200+ line diff across 4 unrelated subsystems,
and getting StubSettings updates wrong silently breaks the test suite at
compile time. Better as a focused PR that can be reviewed against the
ISettings contract directly.

**Suggested approach:** One PR per controller pair (e.g. editor + general
first, then snapping pair, then tiling pair). For each:

1. Add the needed `virtual T methodName() const = 0` declarations to
   `ISettings.h`.
2. Mark Settings's existing methods `override` (grep + 1-line patches).
3. Add no-op stubs to `StubSettings.h` (one-liners that return defaults).
4. Switch the controller's ctor parameter + member type.
5. Drop the now-unused `#include "settings.h"` from the controller header.
6. Run the controller's unit tests against `StubSettings` to confirm the
   abstraction is complete.

---

## 2. Sync → async D-Bus on save / discard (D8, D10 — MEDIUM)

**Finding:**

- `WindowRuleController::commit()` calls `DaemonDBus::callDaemon` synchronously
  via `setAllRules`. Called from `SettingsController::save()` on the Save
  button's handler. Blocks the UI thread until the daemon acknowledges (or
  the call times out). A stuck/firewalled daemon freezes the Settings window.
- `AnimationsPageController::revertPending()` runs synchronous file I/O
  (`QSaveFile::write`) on the GUI thread, one round per snapshot entry. A
  motion-set apply that touched dozens of profile paths means dozens of disk
  round-trips when the user clicks Discard.

**Why deferred:** Both are *behavioural* refactors, not pure cleanups.

1. `StagingDomain::apply()` returns `void`. The current sync paths use the
   bool return from `commit()` to decide whether to keep the page dirty so
   the user knows the save failed. Going async needs a separate
   "commit-in-flight" + "commit-failed" state on the controller, plus
   framework hooks to surface that to the footer.

2. There's a specific `daemonChangedWhileDirty` refusal path that currently
   lives behind the sync return. Threading that through an async pipeline
   cleanly requires reworking the page's whole dirty-state machine.

3. Downstream QML pages observe `commit()` / `revertPending()` completion
   *implicitly* through `dirtyChanged`. Going async changes observable
   timing in ways the QML likely depends on (toast firing, button enabled
   state).

**Suggested approach:**

1. Extend `StagingDomain` with an optional `applyResult(bool ok, QString
   error)` signal that the framework's `ApplicationController::applyAll`
   can wait on (per-domain timeout, fan-in into a single "save complete"
   surface).
2. Convert `WindowRuleController::commit()` to `QDBusPendingCallWatcher`-
   based async; on `finished` emit either `applyResult(true, {})` or
   `applyResult(false, errorString)`. Keep the bool-return overload as a
   sync helper for `SettingsController::save()` until the chrome migrates.
3. Move `AnimationsPageController::revertPending()`'s `QSaveFile` writes to
   a `QtConcurrent::run` worker that emits a `discardComplete` signal. The
   QML Discard button enters a brief "discarding..." state during the
   worker.
4. Update the chrome footer to surface the in-flight state.

Estimate: ~200 LOC + state machine + QML wiring updates. Expect at least
one round of regression fixes — the Save flow is well-covered by manual
testing but not by automated tests today.

---

## 3. WindowRuleController commit() force-overwrite reachability (D4 — CLOSED in Pass 2d)

**Resolution:** Pass 2d added `Q_INVOKABLE bool forceCommit()` on
`WindowRuleController` (header line 126, implementation in
`windowrulecontroller.cpp`). The escape hatch is now reachable from QML.

**Remaining follow-up:** `WindowRulesPage.qml` still needs to wire a
"Save anyway" button (or similar) that calls `controller.forceCommit()`
when `daemonChangedWhileDirty` is true — only the C++ exposure landed
in Pass 2d. Add a unit test once the QML side is wired.

---

## 4. SnappingShadersPageController layout reconnects O(N) (D11 — CLOSED in Pass 2d)

**Resolution:** Pass 2d added `QSet<QObject*> m_wiredLayouts` tracking
plus a `QObject::destroyed` eviction slot, so `connectLayoutSignals()`
now does an O(new-layouts) walk on each `contentsChanged`, not O(N).

**Remaining follow-up:** None — finding fully addressed.

---

## 5. WindowRuleController::duplicateRule single-emit (D12 — PARTIAL Pass 2d)

**Partial Pass-2d fix:** Added `renormalizePriorities()` after the clone
+ reorder sequence — closes the priority-collision concern (clone no
longer shares the source's band-base priority verbatim).

**Still open:** The underlying multi-emit pattern remains. `duplicateRule`
issues `addRule` (one `rowsInserted`) plus up to two `moveRule` calls
(each a `rowsMoved` cycle) plus `renormalizePriorities` (a `dataChanged`
emit). Worst case: four model signals per single user "Duplicate" click.

**Suggested approach (unchanged from original):**

1. Add `WindowRuleModel::addRuleAt(const Rule& rule, int insertIndex)`
   that does a single `beginInsertRows` / `endInsertRows` pair.
2. Replace `duplicateRule()`'s `addRule` + 2 `moveRule` sequence with one
   `addRuleAt` call. The priority renormalization can stay or be folded
   into `addRuleAt` for atomicity.
3. Update any other call site that needed to construct + reposition.

---

## 6. monitorOverview identity-default for lookups (D13 — LOW)

**Finding:** The reviewer flagged that `monitorOverview()` returns raw
UUID-with-braces strings if `setScreenLookup` / `setActivityLookup` /
`setLayoutLookup` haven't been called yet.

**Status:** On re-read after the audit pass closed, the current behaviour
*is* the identity default the reviewer recommended — when `m_layoutLookup`
is null, the `layoutLabel` passes through unchanged. The reviewer's concern
was whether QML renders before lookups are wired; the QML page bootstrap
order needs auditing to confirm this never happens in practice.

**Suggested approach:** Add a `lookupsReady` boolean signal on
`WindowRuleController` that QML's WindowRulesPage waits on before
populating its model. If the controller boots without lookups, the QML
shows a placeholder instead of raw UUIDs.

---

## 7. Sidebar.qml extraction (B9 — LOW)

**Finding:** `libs/phosphor-settings-ui/qml/Sidebar.qml` sits near the
800-line cap (verify with `wc -l`). The ItemDelegate `delegate: ...`
block (~250 lines) is a natural extraction point. Reference symbols
rather than line numbers to avoid further drift — find via
`grep -n "delegate:" Sidebar.qml`.

**Suggested approach:**

1. Extract the ListView delegate into a sibling `SidebarRow.qml`.
2. Pass the 9 row roles + `compact` + `trailingDelegate` + the click
   handler as a signal.
3. Sidebar.qml drops to ~470 lines, the delegate's internals become
   independently testable.

Worth doing before the next visual-tweak pass pushes the file past 800.

---

## 8. Loader.onLoaded width-binding idiom (B10 — LOW)

**Finding:** `AboutPageShell.qml` (around `topLoader.onLoaded`) and
`Sidebar.qml` (around `footerLoader.onLoaded`) both do the same 4-line
"bind item.width to loader width" pattern. Two copies of subtle layout
snippets is where silent visual regressions hide. References are by
symbol — line numbers drift each rebase.

**Suggested approach:** Extract a tiny helper into a shared QML JS file
(e.g. `internal/LoaderHelpers.js`) and call it from both `onLoaded`
handlers.

---

## 9. PageAdapter per-page dirty tracking (D6 — CLOSED, rationale in headers)

**Resolution:** `PageAdapter::isDirty()` always returns `false` and apply/discard
are no-ops by design. The trade-off is documented in both
`src/settings/pageadapter.h` and `src/settings/pageadapter.cpp` (which
cross-references `src/settings/settingsstagingdomain.h`). PlasmaZones
centralises dirty tracking in `SettingsStagingDomain`; PageAdapters are
framework-identity wrappers that don't participate in per-page dirty.

Future consumers of the lib that need per-page dirty UX are not blocked
— `PageController` (which `PageAdapter` derives from) supports per-page
dirty natively, and the animations page demonstrates the pattern.

**Action:** None — closed.

---

## 10. Old-style D-Bus connect calls (E9 — partial)

**Status:** Audit Pass 1o introduced a `subscribeDaemonSignal` lambda inside
`SettingsController`'s ctor that factored out the canonical
`service+path+iface` tuple. 12 inline `QDBusConnection::sessionBus().connect`
blocks collapsed to one-liners.

**What's left:** `QDBusConnection::connect`'s API is fundamentally
string-based (signal name + SLOT() signature). There's no
member-function-pointer overload because D-Bus signals are dynamically
named. This is a Qt API limitation, not a fixable code smell.

A future possibility: add a typed wrapper at the PhosphorProtocol layer
(`libs/phosphor-protocol/include/PhosphorProtocol/ClientHelpers.h`) that
takes a Qt signal name + slot member pointer and synthesises the SLOT
signature internally. Doesn't eliminate strings entirely (the signal name
on the wire is still a string), but localises the marshalling.

---

## 11. NIT items deferred for cosmetic reasons

A handful of NIT findings remain explicitly unaddressed:

- A15: `recomputeDirty` is O(N²) in domain count during a transaction.
  Real cost is negligible for N < 20. If domain count grows past ~100,
  block signals during the loop and emit a single recompute at the end.
- A17: `pageRegistered` signal exists despite "registry is read-only after
  startup" doc. Either remove or document the dynamic-registration use case.
- A18: Mixed member-init style on `ApplicationController`. Pick one
  (default-member-init for all PODs is the modern norm).
- E27/E32: `aspectRatioLabels` / `_aspectRatioOptions` duplicated keys;
  `Qt.rgba(theme, theme, theme, alpha)` copy-paste. Both extractable to
  small QML helpers (`SettingsTheme.qml`).
- E37: Document the `Qt.callLater` pattern around `layoutContextMenu` →
  `aspectRatioSubMenu` reparenting (the SIGSEGV-avoidance reason).

---

## Tracking

These items should be filed as GitHub issues with the `phosphor-settings-ui`
label once PR #533 merges. Each maps to one or more audit findings (audit
codes preserved above so reviewers can cross-reference the original report).

Last updated: 2026-05-26 (post-Pass 2g — broader re-audit applied
correctness fixes that were not in the original deferral set; the
architectural items above remain explicitly out of scope for the
PR-#533 merge).
