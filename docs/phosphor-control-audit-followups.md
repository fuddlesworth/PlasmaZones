# PR #533 audit follow-ups

Items the `/code-audit` pass on PR #533 (`feature/phosphor-control`) flagged
but explicitly deferred. Each entry names the original audit finding code, the
scope of work required, the rationale for deferring, and the suggested approach
when the follow-up PR lands.

Recorded after audit Pass 1 closed with 210/210 tests passing. None of the
items below are correctness blockers for the merge of PR #533 — they are
architectural debt, behavioural refactors with non-trivial regression risk,
or design decisions the audit could not unilaterally make.

---

## 1. ISettings* across all page controllers (D5 — CLOSED pass 21-25)

All seven settings-app page controllers migrated:
- Pass 21 (F#1a): `GeneralPageController` → `ISettings&`.
- Pass 22 (F#1b): `Snapping/TilingBehaviorController` → `ISettings*`.
- Pass 24 (F#1e + F#1d + F#1f): `TilingAlgorithmController` +
  `SnappingAppearanceController` → `ISettings*`. ISettings widened
  with `autotilePerAlgorithmSettings` + `loadColorsFromFile`.
  `TilingAppearanceController` was already parameter-less — closed
  as no-op.
- Pass 25 (F#1c): `EditorPageController` → `ISettings&`. ISettings
  widened with 11 editor getter/setter pairs + 11 NOTIFY signals;
  Settings's existing methods marked `override`; StubSettings grew
  the matching round-trip stubs.

---

### 1.1 Original spec (HIGH at intake)

**Finding:** Six page controllers (`EditorPageController`,
`GeneralPageController`, `SnappingAppearanceController`,
`SnappingBehaviorController`, `TilingAlgorithmController`,
`TilingAppearanceController`, `TilingBehaviorController`) take `Settings*` as
their dependency. `AnimationsPageController` is the lone consumer of
`ISettings*`. CLAUDE.md says:

> Settings access from page controllers must go through `ISettings` virtual
> methods, not the concrete `Settings` class.

**Scope of work:**

- `ISettings` (`src/core/isettings.h`) needs to grow additional pure virtual
  getter+setter pairs to cover the editor / general / snapping-appearance /
  snapping-behavior / tiling-* surface those six controllers use. (Line and
  override counts intentionally elided here — they were snapshots from
  Pass 1 and have since drifted as the migration progressed; see the
  current file for the live numbers.)
- Every new virtual needs a matching `override` declaration in `Settings`
  (`src/config/settings.h`). The methods already exist as non-virtual on the
  concrete class — only the `virtual` keyword + `override` are new.
- `tests/unit/helpers/StubSettings.h` needs a matching stub override for
  every new pure virtual. Without that the StubSettings becomes abstract
  and every test that instantiates it fails to compile.
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

## 2. Sync → async D-Bus on save / discard (D8, D10 — CLOSED passes 23 + 27 + 28)

**Resolved:**
- Pass 23 (F#2a): `StagingDomain::applyResult(ok, error)` +
  `discardResult(ok, error)` signal foundation.
- Pass 27 (F#2b): `RuleController::asyncCommit()` Q_INVOKABLE
  dispatches `setAllRules` via `QDBusPendingCallWatcher` and emits
  `applyResult` on the reply. `pushToDaemonAsync()` mirrors the
  client-side validation of the sync path.
- Pass 27 (F#2c): `AnimationsPageController::asyncRevertPending()`
  runs the QSaveFile restore loop on a `QtConcurrent` worker; a
  `QFutureWatcher` on the GUI thread installs the retained map +
  emits `discardResult`.
- Pass 28 (F#2 chrome): ApplicationController gained
  `applying` / `discarding` Q_PROPERTY booleans, `applyAllAsync` /
  `discardAllAsync` Q_INVOKABLE slots, and
  `applyAllComplete(ok, errors)` / `discardAllComplete(ok, errors)`
  signals. UnsavedChangesFooter Save/Discard buttons now show
  "Saving…" / "Discarding…" while in flight (disabled), and call
  the async variants. SettingsAppWindow's close-prompt waits for
  applyAllComplete/discardAllComplete before closing — gated on a
  closeFlow state-pair so footer-driven Saves don't also close
  the window. Existing `apply()`/`discard()` slots on
  RuleController / AnimationsPageController / SettingsStagingDomain
  bridge to the new contract (sync domains emit the result signals
  inline; async domains emit on their watcher reply). 4 new tests
  pin the async batch contract (complete-once, error collection,
  no-op clean batches emit immediately).

### 2.1 Original spec (MEDIUM at intake)

**Finding:**

- `RuleController::commit()` calls `DaemonDBus::callDaemon` synchronously
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
2. Convert `RuleController::commit()` to `QDBusPendingCallWatcher`-
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

## 3. RuleController commit() force-overwrite reachability (D4 — CLOSED pass 17)

**Resolution:** Pass 2d added `Q_INVOKABLE bool forceCommit()` on
`RuleController`. Pass 17 wired the consumer side:
RulesPage.qml's `daemonChangedWhileDirty` banner now exposes
"Save anyway" + "Discard and reload" actions; "Save anyway" opens a
PromptDialog explaining the overwrite, then calls `forceCommit()`.
Pass 17 also added a unit test pinning the Q_INVOKABLE marker so a
future refactor that drops it fails at the unit-test layer.

---

## 4. SnappingShadersPageController layout reconnects O(N) (D11 — CLOSED in Pass 2d)

**Resolution:** Pass 2d added `QSet<QObject*> m_wiredLayouts` tracking
plus a `QObject::destroyed` eviction slot, so `connectLayoutSignals()`
now does an O(new-layouts) walk on each `contentsChanged`, not O(N).

**Remaining follow-up:** None — finding fully addressed.

---

## 5. RuleController::duplicateRule single-emit (D12 — CLOSED pass 16)

**Resolution:** Pass 16 added `RuleModel::addRuleAt(rule,
insertIndex)` with a single `beginInsertRows`/`endInsertRows` pair
plus clamped index. `duplicateRule` now locates the source's index
once and inserts directly at `sourceIndex + 1`, eliminating the
prior addRule + 2 moveRule sequence (was up to 4 model signals per
Duplicate click; now 1 + the existing renormalize dataChanged). New
unit test pins the single-emit + index-clamp contract.

### 5.1 Original spec

**Partial Pass-2d fix:** Added `renormalizePriorities()` after the clone
+ reorder sequence — closes the priority-collision concern (clone no
longer shares the source's band-base priority verbatim).

**Still open:** The underlying multi-emit pattern remains. `duplicateRule`
issues `addRule` (one `rowsInserted`) plus up to two `moveRule` calls
(each a `rowsMoved` cycle) plus `renormalizePriorities` (a `dataChanged`
emit). Worst case: four model signals per single user "Duplicate" click.

**Suggested approach (unchanged from original):**

1. Add `RuleModel::addRuleAt(const Rule& rule, int insertIndex)`
   that does a single `beginInsertRows` / `endInsertRows` pair.
2. Replace `duplicateRule()`'s `addRule` + 2 `moveRule` sequence with one
   `addRuleAt` call. The priority renormalization can stay or be folded
   into `addRuleAt` for atomicity.
3. Update any other call site that needed to construct + reposition.

---

## 6. monitorOverview identity-default for lookups (D13 — CLOSED pass 15)

**Resolution:** Pass 15 added `lookupsReady()` signal on
`RuleController` that fires exactly once after screen +
activity + snapping-layout + tiling-algorithm resolvers are all
wired. QML pages gate "show raw model" on this so the brief
startup window where lookups would return raw UUIDs / wire tokens
never becomes user-visible. Bit-mask tracking in the controller
keeps the contract install-once.

### 6.1 Original spec

**Finding:** The reviewer flagged that `monitorOverview()` returns raw
UUID-with-braces strings if `setScreenLookup` / `setActivityLookup` /
`setLayoutLookup` haven't been called yet.

**Status:** On re-read after the audit pass closed, the current behaviour
*is* the identity default the reviewer recommended — when `m_layoutLookup`
is null, the `layoutLabel` passes through unchanged. The reviewer's concern
was whether QML renders before lookups are wired; the QML page bootstrap
order needs auditing to confirm this never happens in practice.

**Suggested approach:** Add a `lookupsReady` boolean signal on
`RuleController` that QML's RulesPage waits on before
populating its model. If the controller boots without lookups, the QML
shows a placeholder instead of raw UUIDs.

---

## 7. Sidebar.qml extraction (B9 — CLOSED pass 19)

**Resolution:** Pass 19 extracted the ~270-line ListView delegate
into `SidebarRow.qml`. The 9 row roles + 4 visual settings (compact,
navRowHeight, accentBarWidth, trailingDelegate) are required
properties; navigation intent flows back through three signals
(`navigationRequested`, `categoryToggleRequested`,
`drillIntoRequested`) so the row never reaches into Sidebar's
internals. The model role formerly named `id` was renamed to
`pageId` to fix a qmlformat 6.11 silent-fail bug where `id` shadowed
the QML `id:` directive. Sidebar.qml is now ~480 lines, well under
the 800-line cap.

### 7.1 Original spec

**Partial Pass-10 fix:** The back-button block (~75 lines) was
extracted to `SidebarBackButton.qml` to bring Sidebar.qml from 809
lines (over the 800 cap) down to ~735 lines.

**Still open:** The ListView `delegate: ...` block (~250 lines) is the
larger extraction target. Reference symbols rather than line numbers
to avoid further drift — find via `grep -n "delegate:" Sidebar.qml`.

**Suggested approach:**

1. Extract the ListView delegate into a sibling `SidebarRow.qml`.
2. Pass the 9 row roles + `compact` + `trailingDelegate` + the click
   handler as a signal.
3. Sidebar.qml drops to ~470 lines, the delegate's internals become
   independently testable.

Worth doing before the next visual-tweak pass pushes the file past
800 again.

---

## 8. Loader.onLoaded width-binding idiom (B10 — CLOSED passes 18 + 19)

**Resolution:** Pass 18 created `libs/phosphor-control/qml/LoaderHelpers.js`
with `bindItemWidthToLoader(loader)` and migrated AboutPageShell.qml.
Pass 19 migrated Sidebar.qml's footerLoader (deferred from pass 18
because of an unrelated qmlformat block that pass 19 unblocked).

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

## 12. PhosphorControlQml SHARED-variant install (Pass 10 — CLOSED pass 20)

**Resolution:** Pass 20 added the `PHOSPHOR_CONTROL_QML_INSTALL`
option (default OFF). When enabled it builds a parallel
`PhosphorControlQmlShared` target alongside the STATIC variant +
installs the plugin .so / qmldir / qmltypes under
`${KDE_INSTALL_QMLDIR}/org/phosphor/control/`. Mirrors the
phosphor-animation pattern verbatim, including the `$ORIGIN`
INSTALL_RPATH on the plugin so it finds the sibling shared lib at
runtime. In-tree consumers keep linking the STATIC target — no
behavioural change with the option off.

### 12.1 Original spec

**Finding:** The `PhosphorControlQml` STATIC target's namespaced
alias `PhosphorControl::PhosphorControlQml` works for in-tree
consumers (PlasmaZones + examples/minimal) but is NOT installed and
NOT added to the `PhosphorControlTargets` export set. An
out-of-tree consumer doing `find_package(PhosphorControl)` +
`target_link_libraries(... PhosphorControl::PhosphorControlQml)`
fails at configure with "Target not found".

**Pass-10 partial fix:** The CMakeLists.txt now carries a prominent
"IN-TREE LINKABLE ONLY" docstring on the QML module section so the
contract is explicit rather than misleading.

**Suggested approach:** Mirror `libs/phosphor-animation/CMakeLists.txt`'s
PHOSPHOR_ANIMATION_QML_INSTALL pattern — add a parallel
`PhosphorControlQmlShared` target whose plugin .so + qmldir +
qmltypes are installed under `${KDE_INSTALL_QMLDIR}/org/phosphor/control/`.
Gated behind a PHOSPHOR_CONTROL_QML_INSTALL option, default false.
Lands when the first out-of-tree consumer needs it.

---

## 13. Rule monitorOverview layout-token lookup contract (Pass 10 — CLOSED pass 15)

**Resolution:** Pass 15 split the RuleController lookup into
`setSnappingLayoutLookup` (UUIDs) + `setTilingAlgorithmLookup`
(algorithm tokens). `monitorOverview` picks the appropriate one
based on `rule.engineMode`. RuleModel mirrors the split:
`setSnappingLayoutLabelLookup` / `setTilingAlgorithmLabelLookup`;
`actionLabel` takes both and picks per ActionType. The combined
`setLayoutLookup` / `setLayoutLabelLookup` back-compat shims were
later removed in favor of the typed pair (no legacy shims).

### 13.1 Original spec

**Finding:** `RuleController::monitorOverview` resolves both
snappingLayout (UUIDs) and tilingAlgorithm (algorithm tokens like
"bsp") through the same `m_layoutLookup` callable. The header
docstring scopes the lookup to layoutIds only — if the wired lookup
fails on algorithm tokens, autotile-pinned monitor tiles show the
raw token instead of a localised name.

**Suggested approach:** Either widen the lookup contract in
`rulecontroller.h` to cover algorithm tokens (the impl branches
on what it sees), or split into two lookups
(`snappingLayoutLookup` + `tilingAlgorithmLookup`) for explicit
type-safety. Cross-partition wiring concern — touches
SettingsController's lookup setup as well.

---

## 14. UnsavedChangesFooter applyAll silent-failure toast (Pass 10 — CLOSED pass 14)

**Resolution:** Pass 14 added the `applyOnCloseFailed(dirtyPageIds)`
signal to `SettingsAppWindow` plus a `collectDirtyPageIds()` helper
that walks the registry via the new `PageRegistry::allPagesData()`
Q_INVOKABLE. `DiscardChangesDialog`'s `onApplyConfirmed` now checks
`controller.dirty` after `applyAll()` and emits the signal instead
of silently re-prompting. PlasmaZones' Main.qml wires the signal to
a toast that names the still-dirty page titles.

### 14.1 Original spec

**Finding:** `SettingsAppWindow.qml`'s onApplyConfirmed calls
`controller.applyAll()` then `Qt.callLater(root.close)`. If applyAll
fails to fully clean dirty (a backend page leaves `m_dirty = true`),
the next `onClosing` re-fires the discard dialog instead of actually
closing — which reads as a UI glitch since the user just clicked
Apply and is now seeing the same prompt again.

**Suggested approach:** After `applyAll()`, check `controller.dirty`
and surface a toast or persistent error banner explaining which page
refused the apply, rather than silently re-prompting.

---

## 15. NIT items (statuses after passes 12-27)

- A15: `recomputeDirty` O(N²) — CLOSED in pass 26. ApplicationController
  now holds an `m_inTransaction` flag set during applyAll/discardAll;
  the inner onDomainDirtyChanged skips its per-edge walk and the
  outer transaction emits a single recomputeDirty at the end. O(N²)
  → O(N) for a batch touching N domains.
- A17: `pageRegistered` signal — CLOSED. The PageRegistry header
  docstring documents the dynamic-registration use case explicitly.
- A18: Mixed member-init style — CLOSED in pass 13. ApplicationController
  members all use default-member-init.
- E27: aspectRatioLabels locale binding — CLOSED in pass 12. Backing
  QtObject's per-property bindings re-evaluate on locale change.
- E32: Qt.rgba theme-tint copy-paste — CLOSED in pass 26. New
  `ThemeHelpers.js` with `withAlpha` / `activeTint` / `hoverTint`
  helpers + named alpha constants. UnsavedChangesFooter,
  SidebarBackButton, SidebarRow migrated; PlasmaZones consumer pages
  can adopt incrementally.
- E37: `Qt.callLater` SIGSEGV-avoidance rationale — CLOSED in pass 13.
  Both call sites in Main.qml now have inline comments explaining the
  pattern.

---

## Tracking

These items should be filed as GitHub issues with the `phosphor-control`
label once PR #533 merges. Each maps to one or more audit findings (audit
codes preserved above so reviewers can cross-reference the original report).

Last updated: 2026-05-27 (after passes 12-39 from the senior PR review).
Pass 39 restructured the H2 duplicates (each "CLOSED + original spec" pair
collapsed into one H2 with an "### N.1 Original spec" subsection) and
renumbered the trailing NIT items section from #11 to #15 so it sorts
after #14 in document order.

Closed: #1 (all sub-passes 1a-1f), #2 (foundation + async siblings +
chrome state machine), #3, #5, #6, #7, #8, #12, #13, #14, A15, A17,
A18, E27, E32, E37. Every actionable item in this document is now
resolved on the `feature/phosphor-control` branch.

#10 (old-style D-Bus connect strings) is the only "still listed"
item, and the spec itself documents it as a Qt API limitation
(QDBusConnection::connect is fundamentally string-based) — not
actionable inside this PR. A typed wrapper at the PhosphorProtocol
layer would be a separate library-level design decision.
