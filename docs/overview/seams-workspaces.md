<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# Overview feature — seam map (branch feat/overview, worktree dynamic-workspaces)

All paths relative to `/home/nlavender/Projects/PlasmaZones/.claude/worktrees/dynamic-workspaces`.

Docs read in full: `docs/dynamic-workspaces.md` (41 lines, user-facing policy) and
`docs/dynamic-workspaces-plan.md` (615 lines, historical design record superseded by PR #990 /
v3.5.0; its §8 "Overview-readiness statement" at lines 550-559 names exactly three things the
overview was expected to consume: the `workspaceMapChanged` stream + `workspaceMap()` replay with
`generation` ordering, the verb set, and per-desktop UUID identity with a live int for
`effects->desktops()` addressing). The plan carries an explicit drift banner (lines 20-42); names
below are verified against the code, not the plan.

---

## 1. libs/phosphor-workspaces — ownership map, API, stream

### WorkspaceMap (pure data model)

`PhosphorWorkspaces::WorkspaceMap`, `libs/phosphor-workspaces/include/PhosphorWorkspaces/WorkspaceMap.h:36`,
impl `libs/phosphor-workspaces/src/WorkspaceMap.cpp` (463 lines).

Members (`WorkspaceMap.h:138-141`):
- `QStringList m_screenOrder` — slice-concatenation order
- `QHash<QString, QList<WorkspaceEntry>> m_slices` — screenId → ordered slice
- `QHash<QString, QString> m_ownerOf` — desktopId → screenId inverse index

`WorkspaceEntry` at `WorkspaceMap.h:17-28`: `desktopId` (braced KWin UUID string as KWin reports
it), `name` (empty = dynamic; non-empty = named and destroy-exempt), `homeScreenId` (set only
while displaced by hotplug).

Public API:
- Screen order: `screenOrder()` :43, `setScreenOrder(order)` :48 (screens absent from `order` but
  holding slices are appended in previous relative order, so no slice is orphaned)
- `hasScreen(screenId)` :56 vs `knowsScreen(screenId)` :61 — the distinction is load-bearing:
  a live screen that owns no desktop yet answers false to `hasScreen`. Ask `knowsScreen` before
  routing a desktop to a screen.
- `slice()` :62, `sliceSize()` :63, `ownerOf(desktopId)` :65, `sliceIndexOf(desktopId)` :67,
  `allDesktopIds()` :69, `entryFor(desktopId)` :72
- Mutators (reconciler only): `insert(screenId, sliceIndex, entry)` :78,
  `remove(desktopId)` :96, `reorderWithinSlice(desktopId, newSliceIndex)` :98,
  `transfer(desktopId, toScreenId, sliceIndex)` :100, `setName()` :101, `setHomeScreen()` :102,
  `takeSlice(screenId)` :105, `clear()` :106
- `globalPositionForInsert(screenId, sliceIndex)` :113 — the single place encoding KWin's 0-based
  D-Bus insert position
- `consistentWith(kwinIds)` :117 (diagnostic/tests only; production repairs unconditionally),
  `repairAgainst(kwinIds)` :127 returning unowned ids in KWin order for the caller to adopt
- `toJson(generation, currentByScreen, indexOf, includeState)` :131 and `fromJson(json)` :136 —
  wire and state file share this serializer; `includeState` adds the `homeScreen` fields.

Trap documented in-header (`WorkspaceMap.h:82-94`): `remove()` returns false both when the
desktop was unowned AND when the inverse index named a slice not actually holding the id (stale
row dropped with a warning). That drop is deliberately silent — `toJson` is built from screen
order + slices alone, so repairing the index changes nothing observable and announcing it would
bump the generation for a byte-identical payload. Per-desktop bookkeeping kept beside the map
must not be gated on this return value.

No Qt signals on this class at all.

### WorkspaceReconciler (lifecycle state machine + echo ledger)

`PhosphorWorkspaces::WorkspaceReconciler`, `WorkspaceReconciler.h:39`. Impl split across
`src/WorkspaceReconciler.cpp` (1072), `src/WorkspaceReconcilerNamed.cpp` (257),
`src/WorkspaceReconcilerScreens.cpp` (209).

Constants (`WorkspaceReconciler.h:46-84`):

| Constant | Value | Line |
|---|---|---|
| `LedgerTimeoutMs` | 2000 | :46 |
| `DestroyDebounceMs` | 300 | :47 |
| `MaxRemovalRefusals` | 3 | :54 |
| `MaxCreateRefusals` | 3 | :65 |
| `DefaultDesktopCap` | 20 | :71 |
| `CapProbeExpiries` | 2 | :77 |
| `MaxNamePushRefusals` | 3 | :84 |

`DefaultDesktopCap` is a STARTING value only — the real ceiling is learned from the compositor at
runtime inside `expireLedger` and forgotten when a create succeeds or the live count exceeds it
(`WorkspaceReconciler.cpp:213-216`, `:701-704`). The settings app reads the same constant for its
cap badge: `src/settings/controller/settingscontroller_pagekeys.cpp:74` and `:85`, declared at
`src/settings/controller/settingscontroller.h:346`.

`NamedWorkspace` struct at `WorkspaceReconciler.h:20-25`: `name` (unique, non-empty), `outputId`
(pinned screen; empty = unpinned), `position` (-1 = before the trailing empty).

Accessors: `map()` :86/:87, `generation()` :88, `setDesktopCap(cap)` :96 (test-only seam; nothing
in the daemon calls it), `setFocusedScreen(screenId)` :99.

Inputs (plain calls, no D-Bus — unit tests drive them directly), :101-141:
- `onDesktopListSettled(QStringList ids)` :105 — computes old→new int renumber mapping from the id
  delta, repairs the map, adopts unowned ids, runs invariant maintenance
- `onKwinDesktopCreated(id)` :107 / `onKwinDesktopRemoved(id)` :109 — early id-only echoes
- `onScreenDesktopReport(screenId, desktop)` :113 — returns true when the report was a matched
  echo of our own SetCurrent; the caller then skips its own reactive policy
- `onPopulationChanged(desktopId, windowCount)` :115
- `onScreenAdded(screenId)` :116 / `onScreenRemoved(screenId)` :117 / `onScreenOrderChanged(order)` :119
- `adoptAll(ids, currentDesktopIdByScreen)` :124
- `applyNamedWorkspaces(declarations, kwinNames)` :141 — `kwinNames` MUST be
  `VirtualDesktopManager::rawDesktopNames()`, where an empty entry means unnamed. Passing the
  display form breaks identity (a workspace declared "Desktop 3" would claim an unnamed desktop
  whose placeholder happens to read that way).

Verb-support surface — the natural read/act API for an overview, :143-172:
- `hasPendingStructuralOps()` :146 — true while any create/remove is open
- `currentDesktopIdOf(screenId)` :149
- `desktopIdAtOffset(screenId, delta)` :153 — walks the screen's OWN slice, foreign desktops
  skipped by construction, empty at the slice edge (no wrap, niri semantics)
- `desktopIdAtSliceIndex(screenId, sliceIndex)` :155
- `issueSetCurrent(screenId, desktopId)` :160 — refused (false) while a SetCurrent for that screen
  is already open; the single-correction rule that breaks re-assertion loops
- `snapBack(screenId)` :164
- `reorderCurrentWorkspace(screenId, delta)` :167 — map order only; KWin global order self-repairs
- `transferCurrentWorkspace(screenId, targetScreenId)` :172 — returns the moved desktop id;
  window relocation is the controller's job, this is the map half only

Signals, :174-201:

| Signal | Line |
|---|---|
| `requestCreateDesktop(uint position, QString name)` | :176 |
| `requestRemoveDesktop(desktopId)` | :177 |
| `requestSetCurrent(screenId, desktopId)` | :178 |
| `requestSetDesktopName(desktopId, name)` | :181 |
| `mapChanged()` | :185 |
| `renumberComputed(QHash<int,int> oldToNew, QList<int> removed)` | :188 |
| `foreignSwitchDetected(screenId, desktopId, ownerScreenId)` | :191 |
| `removalRaceDetected(desktopId, ownerScreenId)` | :197 |
| `capReached()` | :199 |
| `resyncRequested()` | :201 |

Private ledger internals worth knowing: `PendingOp` struct :204-225 with
`Kind{Create, Remove, SetCurrent}`, `globalPosition` :222 (the settle path matches open Creates by
GLOBAL POSITION, not ledger FIFO order, because ledger order and settled-list order are
independent rankings), `takeSettledCreates(newIds)` :253, `applySettledCreate` :256,
`maintainInvariants()` :258, `trailingEmptyOf(screenId)` :264,
`insertIndexBeforeTrailingEmpty(screenId)` :267, `namedSliceIndex()` :271, `adoptExternal()` :275,
`evaluateForeign(screenId)` :280. State members :282-325 include `m_lastOwnedByScreen` (the
snap-back target), `m_racedDesktops`, `m_namePushes`, `m_removalRefusals`, `m_createRefusals`,
`m_capProbeIds`.

### VirtualDesktopManager (KWin D-Bus wrappers + id/int translation)

`libs/phosphor-workspaces/include/PhosphorWorkspaces/VirtualDesktopManager.h` (239 lines),
impl 789 lines.

- `init()` :40, `start()` :42, `stop()` :45, `isAvailable()` :136
- `currentDesktop()` :47, `currentDesktopForScreen(screenId)` :48,
  `hasScreenDesktopReport(screenId)` :53, `perScreenModeActive()` :54
- `updateScreenDesktop(screenId, desktop)` :61, `removeScreenDesktop(screenId)` :67,
  `renameScreen(oldId, newId)` :82
- `setCurrentDesktop(int)` :84, `setCurrentDesktopById(id)` :89
- `createDesktop(uint position, QString name)` :95, `removeDesktop(desktopId)` :98,
  `setDesktopName(desktopId, name)` :100
- Translation: `desktopIds()` :106, `desktopIdAt(int)` :108, `desktopIndexOf(id)` :110,
  `desktopCount()` :112, `desktopRows()` :118
- `rawDesktopNames()` :124 (KWin's raw, empty = unnamed) vs `desktopNames()` :130 (display form)

Signals :139-167: `currentDesktopChanged(int)`, `desktopCountChanged(int)`, `desktopsChanged()`,
`screenDesktopChanged(screenId, desktop)`, `kwinDesktopCreated(id)`, `kwinDesktopRemoved(id)`,
`desktopListChanged(QStringList)`. Private: `applyDesktopListReply(QDBusMessage)` :190,
`clampScreenDesktopsToCount()` :201, `resolveCurrentFromId()` :206.

### WorkspaceController (daemon glue, GPL)

`PlasmaZones::WorkspaceController`, `src/daemon/controllers/workspacecontroller.h:39`,
impl `workspacecontroller.cpp` (868) + `workspacecontroller_verbs.cpp` (520).

Constructed only when the feature is enabled — the gate wraps the object, not scattered ifs.
Ctor `(VirtualDesktopManager*, WindowRegistry*, ScreenManager*, parent)` :44.

- `static bool kwinPerOutputEnabled()` :52 — reads kwinrc `PerOutputVirtualDesktops`
- `start()` :57 — census seed, screen order, first-run adoption deferred until every known screen
  has reported a current desktop, with a timeout fallback
- `isAdopted()` :66 — until adoption completes the controller emits no reap and no renumber at
  all, so the daemon's count-based desktop sweeps must keep running. Adoption can take up to
  three seconds (the per-output report grace).
- `setWindowScreenResolver(fn)` :73, `setWindowStickyPredicate(fn)` :80
- `currentMapJson()` :83 — the adaptor's replay source

Verbs :85-102 (screenId is the PHYSICAL id of the acting screen; all walk the screen's own slice
with no wrap and defer behind the reconciler ledger):
`focusWorkspace(screenId, delta)` :90, `focusWorkspaceAt(screenId, sliceIndex)` :92,
`moveWindowToWorkspace(screenId, windowId, delta)` :93,
`moveColumnToWorkspace(screenId, columnWindows, delta)` :97 (columnWindows enumerated daemon-side
from the scroll engine; empty = OSD-level no-op), `moveWorkspace(screenId, delta)` :99,
`moveWorkspaceToOutput(screenId, direction)` :102 ("left"/"right").

Named workspaces :104-155: `applyNamedDeclarations(QVariantList)` :107,
`focusNamedWorkspace(name)` :111, `moveWindowToNamedWorkspace(name, windowId)` :112,
`hasNamedWorkspace(name)` :116, `enum class WorkspaceRouteVerdict{Unresolvable=-1, Unrealized=0}` :119,
`routeWindowToNamedWorkspace(name, windowId, moveOutput, ownerScreenOut)` :154. Note the
`ownerScreenOut` contract at :148-153: written only when a move is actually issued, and a sticky
window returns >0 while writing nothing.

Signals :157-188:

| Signal | Line | Goes to |
|---|---|---|
| `screenDesktopSwitchRequested(screenId, desktop)` | :159 | adaptor `setScreenDesktopRequested` |
| `windowWorkspaceMoveRequested(windowId, targetScreenId, targetDesktop, targetDesktopId, direction, moveOutput)` | :169 | adaptor `moveWindowToWorkspaceVerb` |
| `snapBackOccurred(screenId)` | :173 | OSD hint |
| `windowDisplacedByRemoval(screenId)` | :177 | OSD hint |
| `workspaceMapPublished(mapJson)` | :180 | `WindowTrackingAdaptor::workspaceMapChanged` |
| `desktopsReapRequested(QList<int>)` | :187 | engine fan-out (WHOLE removed set, not per-desktop) |
| `desktopRenumberRequested(QHash<int,int>)` | :188 | engine fan-out |

`desktopsReapRequested` carries the whole batch deliberately (:183-186): the daemon's fan-out
schedules a config save and a state save at its tail, and per-desktop emissions made that N saves,
each preceded by a full `allModes()` scan, for one removal batch.

Private internals of note:
- `static QString canonicalScreenId(connectorOrId)` :196 — the map, census and reconciler all key
  screens by the id the KWin EFFECT reports; ScreenManager and the settings UI hand out other
  spellings. Every externally sourced screen id must pass through here.
- Census: `m_populationById` :230 and `m_windowCensusDesktopId` :235 (keyed by desktop ID STRING,
  not int, so a metadata change adjusts the right bucket after renumbering);
  `censusDesktop(meta)` :207 (0 for sticky/multi-desktop/unknown),
  `adjustPopulationById(desktopId, delta)` :211 — the one census mutation
- `publishIfChanged()` :215, `tryFirstAdoption()` :216,
  `applyNamedDeclarationsAfterAdoption()` :222
- Quiet queue: `runWhenQuiet(fn)` :242, `drainQuietQueue()` :243, `m_draining` :250
- `switchScreenToDesktop(screenId, desktopId)` :246
- Parked routes: `ParkedNamedRoute` :261, `m_parkedNamedRoutes` :266, `drainParkedNamedRoutes()` :267
- Move watchdog: `[[nodiscard]] watchWindowMove(windowId, targetDesktopId)` :282 — returns false
  when the move must NOT be issued (sticky window); every caller uses the answer to skip its emit.
  `m_pendingWindowMoves` :283, `m_windowMoveSequences` :286.
- Reunion: `reuniteWindowWithOwner(instanceId, desktopId)` :294, `m_lastReunionMs` :301,
  `m_pendingReunions` :304
- `DisplacedByRemoval` :305, `m_displacedByRemoval` :312
- State file: `stateFilePath()` :315, `loadStateFile()` :316, `saveStateFile()` :317,
  `scheduleStateSave()` :318, `m_stateSaveTimer` :319

### State file

`~/.local/state/plasmazones/plasmazonesd/workspaces.json` (`QStandardPaths::StateLocation`),
QSaveFile, debounced ~1s. Format = the wire object plus `homeScreen` per displaced entry.
Version mismatch or parse failure means discard and re-adopt. No migration machinery by design.

### Publishing the map to the effect

Daemon relay: `workspacecontroller.cpp:865` emits `workspaceMapPublished`, connected at
`src/daemon/daemon/workspaces.cpp:564`.

Adaptor: `src/dbus/windowtrackingadaptor/windowtrackingadaptor.h:1262` `QString workspaceMap() const`
(replay), `:1269` `void workspaceMapChanged(QString mapJson)`, cache member `:1940`.
Emit site `src/dbus/windowtrackingadaptor/lifecycle.cpp:952`; replay getter `:955`.

XML: `dbus/org.plasmazones.WindowTracking.xml:261-271`.
Payload: `{v, generation, screenOrder, slices: {screenId: [{id, index, name?, current?}]}}`.
`index` is the live 1-based global desktop number for `effects->desktops()` addressing; `id` is
authoritative. Change-gated (only fires on real payload difference). An empty string means the
feature is off (a runtime disable sends one) and tells a cache holder to drop what it has.

Effect consumption, `kwin-effect/plasmazoneseffect/daemon_bringup.cpp`:
- subscribe `:835`
- live handler `:982-1015`: `++m_workspaceMapEpoch` :982, empty-payload clear :996-997,
  non-object rejection :1005, generation ordering guard :1011, store :1014-1015
- bringup replay `:289-311`: captures `replayEpoch` :289, calls `workspaceMap` :292, discards a
  superseded daemon cycle :298 and a live-push supersede :301, warns on failure :311
- generation-guard note for a sibling replay at `:369`; contract comment at `:831`
- cache clear on daemon loss `kwin-effect/plasmazoneseffect/lifecycle_wiring_daemon.cpp:206-207`

Effect members: `plasmazoneseffect.h:3614` `m_workspaceMapJson`, `:3615`
`m_workspaceMapGeneration`, `:3623` `m_workspaceMapEpoch`.

Nothing in the effect reads the cached map today. It is a ready, unclaimed feed for the overview.

### Verbs — shortcut ids and where handled

Ids, `src/daemon/controllers/shortcutmanager_ids.h`:
`workspace_focus_up` :42, `workspace_focus_down` :43, `workspace_move_window_up` :44,
`workspace_move_window_down` :45, `workspace_move_column_up` :46, `workspace_move_column_down` :47,
`workspace_reorder_up` :48, `workspace_reorder_down` :49, `workspace_move_to_monitor_left` :50,
`workspace_move_to_monitor_right` :51. Prefixes: `workspace_focus_slot_` :60 with builder
`workspaceFocusSlotId(slotZeroBased)` :61, the move-slot prefix beside it, and
`workspace_named_focus:` :72 with builder :76 plus a named-move prefix.

Registration, `src/daemon/controllers/shortcutmanager.cpp`:
- `kIndexedSlotCount` static_assert against `ConfigDefaults::WorkspaceSlotCount` :37
- `isWorkspaceShortcutId(id)` :60-69 — deliberately ENUMERATED, not `startsWith("workspace_")`
  (comment :54-59); the two slot families stay prefix-matched
- feature-off park :209-210
- other prefix-matched dynamic ids :409-412
- grab gate: `workspacesOn` :601, `active = workspacesOn || !isWorkspaceShortcutId(e.id)` :618 —
  the workspace family holds a KGlobalAccel grab only while the feature is on
- move-slot loop :687-712 (emits `workspaceMoveSlotRequested(slot)` :709)
- focus-slot loop :714-732 (emits `workspaceFocusSlotRequested(slot)` :732)

Daemon-side handling, `src/daemon/daemon/workspaces.cpp` (995 lines):
`initializeWorkspaces()` :193; enable/consent rearm :229-230;
`desktopsReapRequested` to engine fan-out :270; `desktopRenumberRequested` :397;
`workspaceMapPublished` to adaptor :564; `screenDesktopSwitchRequested` becomes
`Q_EMIT m_windowTrackingAdaptor->setScreenDesktopRequested(screenId, desktop)` :575-578;
`windowWorkspaceMoveRequested` becomes `moveWindowToWorkspaceVerb(...)` :583-587;
`snapBackOccurred` :592; `windowDisplacedByRemoval` :606; `windowActivated` :623;
verb connects `workspaceFocusRequested` :641, `workspaceMoveWindowRequested` :645,
`workspaceMoveColumnRequested` :651, `workspaceReorderRequested` :676,
`workspaceMoveToMonitorRequested` :680, `workspaceMoveSlotRequested` :684,
`workspaceFocusSlotRequested` :699; named-entry rebind :779;
`perOutputDesktopsModeReported` :805; stock-rebind apply :879; `teardownWorkspaces()` :952.
File-locals: `enableKWinPerOutputDesktops()` :67, `saveBoundNamedShortcutIds()` :158,
`restoreKWinShortcutBackup()` :175.

Effect-side workspace verb handling: there is none beyond `slotSetScreenDesktopRequested` and the
window-move slots (section 2). All verb LOGIC is daemon-side; the effect only executes primitives.

---

## 2. Daemon to effect command channel

### Engine base signals

`libs/phosphor-engine/include/PhosphorEngine/PlacementEngineBase.h:94` `Q_SIGNALS:`
- `windowDesktopMoveRequested(windowId, int desktop)` :106
- `crossModeMoveRequested(windowId, targetScreenId, targetDesktop, ...)` :131
- output-move sibling at :141 (`targetDesktop` is 0 for a monitor crossing)

Forwarded to the adaptor in `src/dbus/windowtrackingadaptor/enginewiring.cpp`:
autotile :375-376, snap :379-380, scroll :480-481; disconnects :61, :71, :74; rationale comment
:370-372 ("the KWin effect performs the real `windowToDesktops`"). Direct emits from
`src/dbus/windowtrackingadaptor/crossmode.cpp:152` and
`src/dbus/windowtrackingadaptor/rules_placement.cpp:335`.

### Adaptor declarations (`src/dbus/windowtrackingadaptor/windowtrackingadaptor.h`)

- `setScreenDesktopRequested(screenId, int desktop)` :1278
- `windowDesktopMoveRequested(windowId, int desktop)` :1424
- `windowOutputMoveRequested(windowId, targetScreenId)` :1444
- `moveWindowToWorkspaceVerb(rawWindowId, targetScreenId, targetDesktop, ...)` :1601 — implemented
  `src/dbus/windowtrackingadaptor/crossmode.cpp:61`, fanning out to `windowOutputMoveRequested`
  :105 and :344 and `windowDesktopMoveRequested` :152
- doc references at :1076, :1157

### Effect handlers (`kwin-effect/plasmazoneseffect/daemon_apply.cpp`)

| Handler | Line |
|---|---|
| `emitNavigationFeedback(...)` | :43 |
| `slotActivateWindowRequested(windowId)` | :56 |
| `placeWindowWhereItIs(w)` | :98 |
| `slotWindowDesktopMoveRequested(windowId, int desktop)` | :112 |
| `slotWindowDesktopMoveByIdRequested(windowId, desktopId)` | :142 |
| `applyDesktopMove(w, target, windowId)` calling `effects->windowToDesktops(w, {target})` | :176 / :196 |
| `slotWindowOutputMoveRequested(windowId, targetScreenId)` calling `effects->windowToScreen(w, output)` | :233 / :257 |
| `slotSetScreenDesktopRequested(screenId, int desktop)` | :260 |
| `slotWindowOutputMoveExpected(windowId, targetScreenId, sourceScreenId)` | :314 |
| `slotApplyGeometryRequested(...)` | :331 |
| `slotApplyGeometriesBatch(...)` | :521 |
| `slotRaiseWindowsRequested(windowIds)` | :784 |
| `slotWindowFloatingChanged(...)` | :802 |
| `slotWindowStateChanged(...)` | :942 |
| `slotWindowMinimizedChanged(w)` | :974 |
| `slotRunningWindowsRequested()` | :1081 |

`slotSetScreenDesktopRequested` (:260-311) is the per-output switch and the model for any overview
switch command:
1. resolves the output via `outputForScreenId(screenId)` :265
2. resolves the desktop through the file-local `desktopByNumber(desktop)` :271, which matches
   `VirtualDesktop::x11DesktopNumber()` across `effects->desktops()`, not by position in that list
3. sets `m_programmaticDesktopSwitch = true` under a `qScopeGuard` :286-291 to suppress the
   full-screen `desktop.switch` blend (a corrective bounce costs two output-sized GLTexture
   captures per output otherwise)
4. `KWin::effects->setCurrentDesktop(target, output)` :292
5. reads the live value back and issues a FORCED, unconditional
   `reportScreenDesktop(output, liveDesktop, /*force=*/true)` :309 — this report IS the SetCurrent
   acknowledgement. An already-satisfied request produces no `desktopChanged` at all, so without
   the force the ledger entry would hang until timeout and block that screen.

`slotWindowOutputMoveExpected` (:314-327) hands a one-shot to
`TilingHandler::markExpectedOutputMove(windowId, targetScreenId, sourceScreenId)`, which owns the
cross-output `outputChanged` transfer path that would otherwise re-issue close/open. The scrolling
monitor-crossing verbs ride this.

### Report path back

`PlasmaZonesEffect::reportScreenDesktop(output, desktop, force)`,
`kwin-effect/plasmazoneseffect/screens.cpp:107`. Dedup against `m_lastScreenDesktop` :125; the
`force` escape hatch is explained :117-124. `outputScreenId(output)` resolution just above (with
the duplicate-model `baseId/connector` disambiguation ending :105).
`lastReportedScreenDesktops()` :139 re-resolves ids per call rather than caching, because
`outputScreenId` appends `/connector` only while a duplicate model is connected.

Effect to daemon transport is `PhosphorProtocol::ClientHelpers::fireAndForget(...,
"screenDesktopChanged", {screenId, desktop})` at `screens.cpp:133-135`.

The five-connection `desktopChanged` block lives in
`kwin-effect/plasmazoneseffect/lifecycle_wiring.cpp` (~378/395/407/438/499), with the owner-wins
note at :597. The daemon's `[SEQ A]-[SEQ E]` fan-out lambda is `src/daemon/daemon/start.cpp:335+`.

XML for `setScreenDesktopRequested` at `dbus/org.plasmazones.WindowTracking.xml:273-290` documents
the accepted renumber race: the index is resolved at emit and NOT re-checked, but the reconciler's
ledger matches the answering report by desktop IDENTITY, so a switch landing on the wrong desktop
fails to retire its entry and owner-wins corrects after expiry.

### D-Bus interface inventory (`dbus/`)

`org.plasmazones.Autotile.xml`, `CompositorBridge`, `Control`, `EditorApp`, `LayoutRegistry`,
`Overlay`, `Rules`, `Screen`, `Scrolling`, `SettingsApp`, `Settings`, `Shader`, `Snap`, `Tiling`,
`WindowDrag`, `WindowTracking`, `ZoneDetection`.

`org.plasmazones.Scrolling.xml` — methods: `focusColumn` :23, `scrollView` :33,
`setColumnWidthProportion` :43, `setColumnWidthPixels` :53, `setWindowHeightProportion` :63,
`setWindowHeightPixels` :73, `toggleMaximizeColumn` :83, `toggleMaximizeToEdges` :96,
`clearWindowedFullscreen` :109, `reapplyWindowGeometry` :116, `visibleStripJson` :123,
`presetVocabularyJson` :133, `blueprintProgressJson` :143. Signals: `scrollingScreensChanged` :153,
`stripContextChanged` :160, `scrollEffectBehaviourChanged` :173,
`scrollFocusScrollBlockedWindowsChanged` :180, `leaveNativeFullscreenRequested` :187,
`stripChanged` :194.

`org.plasmazones.WindowDrag.xml` — methods: `beginDrag` :11, `updateDragCursor` :32, `endDrag` :41,
`selectorScrollWheel` :63, `cancelSnap` :70. Signals: `zoneGeometryDuringDragChanged` :74,
`restoreSizeDuringDragChanged` :92, `dragPolicyChanged` :104, `snapAssistReady` :111.

### TilingAdaptor batch holding (the precedent for holding effect-bound batches)

`src/dbus/tilingadaptor/tilingadaptor.h`: batches HELD while a coalesced screens announce is
queued :103; test seam :197; geometry-batch signal :402; drain "emit the batches parked in
`m_tileBatchesHeldForAnnounce`, in order" :540; member :569-572 (a batch that reached the hold
first lost every entry past the one it held, hence the queue); `managedScreensChanged` re-add note
:602. Trusted-peer note on unbounded batch size :262-266.

The replay pattern the workspace stream copies: `scrollTabStrips()` `tilingadaptor.h:328`
(declared :122, cache :578), impl `src/dbus/tilingadaptor/tilingadaptor.cpp:247`, XML
`dbus/org.plasmazones.Tiling.xml:113`, effect side `kwin-effect/tilinghandler/wiring.cpp:657` with
the failure warning :677 and the query-generation counter plus bounded retry around :612-645.

---

## 3. Scroll engine

### Strip exposure for rendering

`ScrollingAdaptor::visibleStripJson(screenId)` — declared
`src/dbus/scrollingadaptor/scrollingadaptor.h:416`, implemented
`src/dbus/scrollingadaptor/scrollingadaptor.cpp:418-480+`.

Gates: empty screenId returns `"[]"` :421; `if (!m_engine->isActiveOnScreen(screenId)) return "[]"`
:432 — load-bearing, not belt-and-braces: `setActiveScreens` prunes only the CURRENT context's
key, so a strip built under a sibling context (another virtual desktop or activity) survives the
screen leaving the active set and would otherwise be answered here.

Payload: ONE walk of `m_engine->visibleTilesWithRects(screenId)` :448 (deliberately one walk, not
paired `visibleTiles` plus `visibleTileRectsRelative`, which resolved the strip twice per call);
per tile `x/y/width/height` relative rect plus `zoneNumber` from `VisibleTile::zoneNumber` (never
re-derived as `i+1`), plus `tabCount`/`activeTab`/indicator-position keys only when
`entry.tile.tabCount > 0` :466-472. The indicator-position wire values are `TabIndicatorPosition`
enumerators from `ScrollTypes.h`, kept in sync BY HAND with the XML and the `StripPreviewKey` doc.

`visibleStripJson` answers only the screen's CURRENT desktop and activity context.
`libs/phosphor-scroll-engine/include/PhosphorScrollEngine/ScrollEngine.h:547` states this
explicitly ("a sibling desktop's strip has its own progress and..."). Same for
`blueprintProgressJson` (XML :144) and the "Same screen gate as focusColumn" comment at
`scrollingadaptor.cpp:483`. There is no per-desktop strip query today — an overview rendering
every workspace's strip has no backing API.

`stripChanged` (XML :194) is a wake-up, not a payload, and is explicitly NOT payload-gated: it
fires on placement/focus changes including ones that change nothing visible (a floating window
move, a non-current context), and it is one-way (a settings/rule/template push that relayouts
without moving the view anchor emits nothing). The XML tells receivers to coalesce it and keep a
periodic re-read as the backstop. `stripContextChanged` :160 is the context-switch signal;
`scrollingScreensChanged` :153 the ownership one.

Settings app already polls this path: `src/settings/controller/settingscontroller_session.cpp:713`
calls `visibleStripJson` on a live timer while its Monitors state view is open.

### Strip model, view offset, columns

`libs/phosphor-scroll-engine/include/PhosphorScrollEngine/ScrollStrip.h`, class at :79.
Headers in the directory: `IScrollSettings.h`, `ScrollEngine.h`, `ScrollEngineTypes.h`,
`ScrollStashTypes.h`, `ScrollState.h`, `ScrollStrip.h`, `ScrollTypes.h`, `StripAxis.h`.

- `const QVector<Column>& columns()` :85
- `scrollViewBy(delta, params)` :719
- `viewOffsetFor(params)` :869, anchor clamp :872
- Tab cycling verb noted :217; tabbed geometry `tabbedColumnCrossPx(c, params)` :857 and
  `tabbedCrossReservationPx(c, params)` :860, with the min-size contract :798-813 and :852-857
- Class doc :30-45: the view anchor is stored RELATIVE TO the focused column and `viewOffset` is
  DERIVED, never stored, so mutations never make the focused window drift. A successful
  `scrollViewBy` DETACHES the view; `updateViewForFocus` reattaches. `:657` and `:752` note
  deliberate out-of-range derived offsets.

Per-context keying is shared with the other engines: `PlacementStateKey{screenId, int desktop,
activity}` at `libs/phosphor-engine/include/PhosphorEngine/EngineTypes.h:21`, held in
`PerScreenStates` (`libs/phosphor-engine/include/PhosphorEngine/PerScreenStates.h`, with
`takeState`/`insertState` and `removeStatesIf(pred)` at :179).

### Drag insert and drop indicator

Engine virtuals, `libs/phosphor-engine/include/PhosphorEngine/IPlacementEngine.h`:
`beginDragInsertPreview(windowId, screenId)` :577, `commitDragInsertPreview()` :583,
`cancelDragInsertPreview(dragStillActive=false)` :594, `dragInsertPreviewScreenId()` :598,
`dragInsertPreviewPriorScreenId()` :611 (distinct whenever the drag crossed screens),
`updateDragInsertPreview(DragInsertTarget)` :629, `dragInsertIndicatorRect(screenId)` :730.
Clamp note against `tiledWindowCount` at :533.

Daemon drives them from `WindowDragAdaptor`:
- engine selection `dragInsertEngineFor(screenId)` `src/dbus/windowdragadaptor/windowdragadaptor.cpp:216`,
  `dragInsertPreviewEngine()` :230, decl `windowdragadaptor.h:524`/:528
- per-move update `src/dbus/windowdragadaptor/drag.cpp:517-552` including hold-grace resolution
  (`m_dragInsertLastHeldMs` :545/:552, decl `windowdragadaptor.h:705`)
- screen matching `windowdragadaptor.cpp:381-382` (`samePhysical` against current AND prior),
  commit path :510-548, toggle reset :588; `m_dragInsertToggled` `windowdragadaptor.h:689`
- indicator push `src/dbus/windowdragadaptor/dragtimers.cpp:99-162`:
  `pushScrollDropIndicator(screenId, engine->dragInsertIndicatorRect(screenId), animate)` :123,
  cross-screen clear :140-151, teardown :156-162; `m_dropIndicatorScreenId` `windowdragadaptor.h:883`
- overlay service call `IOverlayService::updateScrollDropIndicator(screenId, rect, animate)`,
  daemon impl `src/daemon/overlayservice/scrolldropindicator.cpp`, QML
  `src/ui/ScrollDropIndicatorContent.qml`
- selector gate note `src/dbus/windowdragadaptor/selector_gate.cpp:33`
  (`Daemon::dragInsertSelectorForScreen` plus screen-mode routing)

The effect's only role during a drag is reporting (section 4). Preview and indicator logic are
entirely daemon-side.

### moveActiveWindowAcrossBoundary

No symbol by that name exists in this tree. The monitor-crossing path is
`libs/phosphor-tile-engine/src/NavigationController_crosssurface.cpp:315` emitting
`windowDesktopMoveRequested`, documented at
`libs/phosphor-tile-engine/include/PhosphorTileEngine/NavigationController.h:257`, plus
`crossModeMoveRequested` on the engine base.

---

## 4. Snap and tile engines, drag reporting, thumbnails

### Programmatic window relocation

There is no engine-level "move this window to desktop D" that bypasses the effect. All three
engines emit `windowDesktopMoveRequested` and the output-move sibling on `PlacementEngineBase`;
the adaptor forwards; the effect calls `windowToDesktops` / `windowToScreen` (section 2).

The workspace controller's own move rides `moveWindowToWorkspaceVerb`, which carries BOTH the
desktop number and the stable `targetDesktopId` (`workspacecontroller.h:164-168`) so the
compositor-side move can name the desktop rather than a position, plus a `moveOutput` flag so a
route can issue the desktop leg only.

### Drag begin/move/end reporting today

`PlasmaZones::DragTracker`, `kwin-effect/handlers/dragtracker.h:32`. Event-driven off KWin's
per-window `windowStartUserMovedResized` / `windowFinishUserMovedResized` (comment :67-69) —
the poll timer was removed entirely.

- `isDragging()` :39 — the tracker's own shadow
- `compositorMoveResizeActive()` :48 — KWin's interactive state. The header is explicit (:41-51)
  that the two diverge on purpose: `forceEnd()` clears the tracked drag on LMB release while KWin
  keeps its move alive until all buttons are up, and a window `shouldHandleWindow()` rejected is
  never tracked yet still holds KWin's move filter. Consumers that must not steal input from
  KWin's move filter gate on BOTH, OR'd.
- `draggedWindow()` :52, `draggedWindowId()` :56, `lastCursorPos()` :60

The effect forwards to the daemon's `beginDrag` / `updateDragCursor` / `endDrag`
(`org.plasmazones.WindowDrag.xml:11/:32/:41`). Drag-side effect TUs: `mouse_drag.cpp`,
`drag_snap.cpp`, `drag_end.cpp`, `lifecycle_wiring_drag.cpp`.

Keyboard grabbing during a drag, `kwin-effect/plasmazoneseffect/lifecycle_wiring_drag.cpp`:
policy flag `m_currentDragPolicy.grabKeyboard = true` :82; grabs at :198, :320, :343
(`KWin::effects->grabKeyboard(this)`); releases at :235, :395 (`ungrabKeyboard()`).
Trap at :193-197: `grabKeyboard` ANSWERS — it returns false when another effect holds the grab,
and calling `ungrabKeyboard` then would release the OTHER effect's grab. The snap path's policy
always carries `grabKeyboard = true` (:232, :311).

### Window thumbnails — a reusable path exists

`PlasmaZones::SnapAssistThumbnailCapture`,
`kwin-effect/compositor/snapassistthumbnailcapture.h:69` (plus `.cpp`).

Renders each candidate `KWin::EffectWindow` into an offscreen `GLFramebuffer` via
`effects->drawWindow` and reads it back with `GLTexture::toImage()`, reusing the live compositor
texture — no ScreenShot2 D-Bus round trip, no `X-KDE-DBUS-Restricted-Interfaces` gate, no second
scene-graph pass.

Two transports:
- DEFAULT, zero-copy GPU: the FBO texture is exported as a single-plane dma-buf, fd shipped via
  `org.plasmazones.Overlay.setWindowThumbnailDmabuf`; pixels never cross the session bus. Captures
  run as ONE BATCH per show, paying the settle delay and GL-context window once for all candidates.
- FALLBACK, raw pixels: taken automatically when the driver lacks the EGL extensions or the daemon
  repeatedly rejects the import (`onDmabufRejected`). Captures run SEQUENTIALLY, one
  render-plus-readback at a time, posted via `org.plasmazones.Overlay.setSnapAssistThumbnail` as
  raw ARGB32 non-premultiplied bytes plus dimensions — no PNG encode, no base64.
  `PLASMAZONES_DMABUF_THUMBNAILS=0` pins the session to raw pixels.

`Candidate` struct :74 is just a `QUuid internalId` (the EffectWindow internal id for
`effects->findWindow()`); the braced `toString()` is also the daemon's image-provider cache key,
derived once inside `postThumbnail`.

Historical constraint recorded in the header (:37-40): KWin 6.7 removed the public offscreen-QML
readback — `OffscreenQuickView` lost `bufferAsImage()` and `update()` now requires an
`OutputFrame` — so the earlier `OffscreenQuickScene` plus `WindowThumbnail` QML approach is gone.
This is the direct precedent for how an overview would get live window imagery.

Daemon consumer `src/daemon/overlayservice/snapassist.cpp`; QML `src/ui/SnapAssistContent.qml`;
effect handler `kwin-effect/handlers/snapassisthandler.{h,cpp}`.

---

## 5. KWin effect — structure, painting, overlays, gestures

### Directory structure

- `kwin-effect/plasmazoneseffect/` — the effect, ~50 TUs: `plasmazoneseffect.{h,cpp}`,
  `lifecycle.cpp`, `lifecycle_wiring.cpp`, `lifecycle_wiring_daemon.cpp`,
  `lifecycle_wiring_drag.cpp`, `daemon_apply.cpp`, `daemon_bringup.cpp`, `daemon_settings.cpp`,
  `daemon_settings_scrolltabs.cpp`, `paint_pipeline.cpp`, `paint_capture.cpp`,
  `paint_shader_window.cpp`, `paint_internal.h`, `decorations.cpp`, `decoration_render.cpp`,
  `decoration_appearance.cpp`, `decoration_rules.cpp`, `decoration_teardown.cpp`,
  `surface_*.cpp` (audio/backdrop/capture/compile/gating/fold/types), `surfacelayers.cpp`,
  `shader_*.cpp` (config_dbus/resolve/textures/transitions/internal), `screens.cpp`,
  `scroll_clip.cpp`, `mesh_sim.{cpp,h}`, `mouse_drag.cpp`, `drag_snap.cpp`, `drag_end.cpp`,
  `input_filter.{cpp,h}`, `rule_invalidation.cpp`, `window_connections.cpp`,
  `window_desktop_connections.cpp`, `window_filtering.cpp`, `window_identity.cpp`,
  `window_lifecycle.cpp`, `window_query.{cpp,h}`, `effect_state.h`, `types.h`,
  `transition_types.h`
- `kwin-effect/handlers/` — `dragtracker`, `navigationhandler`, `screenchangehandler`,
  `snapassisthandler`, `snaphandler`
- `kwin-effect/compositor/` — `compositorbridge`, `compositorclock`, `deferredwindowcommits.h`,
  `effectlogging.h`, `scrollbehaviourparse.h`, `scrolltabindicatorpainter` (plus `_raster`),
  `snapassistthumbnailcapture`, `stripviewanimator`, `windowanimator`
- `kwin-effect/transitions/` — `desktoptransitionmanager` (plus `capture`/`shader`/`teardown` TUs),
  `shadertransitionmanager`, `striptransitionmanager` (plus `striptransitionshader`),
  `stripmotionsampler.h`, `transitionpasshelpers`
- `kwin-effect/tilinghandler/` — 17 files: `tilinghandler.{h,cpp}`, `tiling.cpp`, `wiring.cpp`,
  `signals.cpp`, `state.cpp`, `scrolltabs.cpp`, `wheelchord.cpp`, `windowedfullscreen.cpp`,
  `outputchange.cpp`, `screenschanged.cpp`, `floatcleanup.cpp`, `minimizefloat.cpp`,
  `pretiledecisions.h`, `pretilegeometry.cpp`, `scrolldecisions.h`
- `kwin-effect/metadata.json`, `kwin-effect/CMakeLists.txt`

### Paint pipeline

`kwin-effect/plasmazoneseffect/plasmazoneseffect.h`: `isActive()` :176, `prePaintScreen` :181,
`postPaintScreen` :183, `prePaintWindow(RenderView*, EffectWindow*, WindowPrePaintData&)` :184,
`paintScreen(...)` :198, `paintWindow(...)` :200.

The class derives from KWin's `OffscreenEffect` and uses the per-window redirect for shader
work (`shader_textures.cpp:334`, `:457`; `paint_shader_window.cpp:85`, `:163-168`, `:215`,
`:952-970`; `paint_capture.cpp:330`, `:373`, `:683`; `decoration_teardown.cpp:183`;
`surface_types.h:608`). Decorations are drawn in `decorations.cpp` / `decoration_render.cpp`, NOT
in `paintWindow` (comment :187-208, "paintWindow no longer touches the border", KDE-Rounded-Corners
model). `paintScreen` is overridden for exactly two cases (:198 comment).

`prePaintScreen` also elects the tab anchor and skips parked columns (:1011), and picks the
topmost scroll-managed window on the pass output as the paint anchor (:1077-1078).
`postPaintScreen` drives the repaint loop and the parked-column GL reap (:1528-1534).
Per-window "burn" gating at :1006-1011 (withholds the TRANSFORMED flag, skips backdrop
capture / decoration fold / draw).

### What does NOT exist

Grepping `QuickSceneEffect`, `registerTouchpadSwipeShortcut`, `registerRealtimeTouchpadSwipeShortcut`,
`isActiveFullScreenEffect`, `setActiveFullScreenEffect`, `grabKeyboard` across `kwin-effect/`,
`src/` and `libs/` returns only `OffscreenEffect` and `grabKeyboard` hits.

- No `QuickSceneEffect` anywhere in the tree.
- No touchpad or realtime gesture registration anywhere.
- No full-screen-effect claim (`setActiveFullScreenEffect` / `isActiveFullScreenEffect` unused).
- Pointer grabbing: none via `effects->` beyond the input filter; keyboard grabbing only in the
  drag path (section 4).

An overview wanting an in-compositor QML scene, a full-screen-effect claim, or a swipe gesture
introduces all three to this codebase.

### Overlays are layer-shell, not in-effect

`libs/phosphor-layer/include/PhosphorLayer/`: `ILayerShellTransport.h`, `IQmlEngineProvider.h`,
`IScreenProvider.h`, `ISurfaceAnimator.h`, `ISurfaceStore.h`, `PhosphorLayer.h`, `Role.h`,
`ScreenSurfaceRegistry.h`, `Surface.h`, `SurfaceConfig.h`, `SurfaceFactory.h`,
`TopologyCoordinator.h` (plus `defaults/`, `Role/`, `Surface/`, `SurfaceConfig/`,
`SurfaceFactory/`, `ScreenSurfaceRegistry/`, `TopologyCoordinator/` subdirs).

`Role.h:28` — `Layer::Overlay = 3`, "Above everything including fullscreen (HUDs, OSDs)";
default `Layer layer = Layer::Overlay` :78; validity constraint :93-94 (Overlay layer rejects a
positive exclusive zone, because Overlay ignores zones). Roles are declared as inline constants,
pattern shown at :71.

Daemon surface host `src/daemon/overlayservice/` (20 TUs): `lifecycle.cpp`, `osd.cpp`,
`overlay.cpp`, `overlay_data.cpp`, `overlay_helpers.h`, `selector.cpp`, `selector_strip.cpp`,
`selector_update.cpp`, `snapassist.cpp`, `cheatsheet.cpp`, `scrolldropindicator.cpp`,
`shader.cpp`, `shellhost_bridge.cpp`, `priming.cpp`, `rekey.cpp`, `screens.cpp`,
`settings.cpp`, `animation_config.cpp`, `internal.h`, `phosphor_roles.h`,
`phosphor_slot_keys.h`, `qml_property_names.h`.

Show pattern:
`m_surfaceAnimator->beginShow(state->shell->shellSurface(), slot, PhosphorRoles::ZoneSelector, cb)`
at `src/daemon/overlayservice/selector.cpp:147` and `:717`.

Shared QML in `src/ui/`: `PassiveOverlayShell.qml`, `PassiveOverlayModalSlots.qml`,
`RenderNodeOverlay.qml` plus `RenderNodeOverlayContent.qml`, `ZoneOverlayContent.qml`,
`ZoneSelectorContent.qml` plus `ZoneSelectorStripCard.qml`, `ZoneItem.qml`,
`LayoutOsdContent.qml`, `LayoutPickerContent.qml`, `NavigationOsdContent.qml`,
`SnapAssistContent.qml`, `ScrollDropIndicatorContent.qml`, `OsdDismissable.qml`,
`CheatsheetContent.qml` plus `CheatsheetGroup.qml`, `CheatsheetRow.qml`,
`CheatsheetSearchField.qml`, `KeyChip.qml`, `PopupCardTitle.qml`.

`src/shell/` is the SEPARATE shell process (bar, control center, launcher, popout transports:
`main.cpp`, `barcontroller`, `controlcentercontroller`, `launchercontroller`,
`layerpopouttransport`, `routingpopouttransport`, `socketpopouttransport`,
`qmlcomponentbarwidgetfactory`, `qmlcomponenttilefactory`). It is NOT where the OSD, picker and
selector surfaces are hosted — those are daemon-hosted layer-shell surfaces.

### Existing desktop-switch animation

`PlasmaZones::DesktopTransitionManager`, `kwin-effect/transitions/desktoptransitionmanager.h:69`.

`begin(from, to, output, effectId, params, durationMs, progressCurve)` :89 — `output` is already
per-output, nullptr meaning a global all-output switch. It resolves nothing itself; the caller
passes the resolved pack id, effective params, motion-cascade duration and timing curve. Every
resolved lifetime is clamped into the animation envelope. Capture is deferred to the first
`paintOutput()` per output, where a live GL context exists (:87-88).

Show-desktop peek variant :93-109: both legs share ONE endpoint pair (FROM = windows scene, TO =
bare desktop) and differ only in time direction; the show leg reuses the hide leg's cached
bare-desktop texture per output because that scene cannot be re-captured once the windows are back.

Companion TUs `desktoptransitioncapture.cpp`, `desktoptransitionshader.cpp`,
`desktoptransitionteardown.cpp`. Suppression flag `m_programmaticDesktopSwitch` set by
`slotSetScreenDesktopRequested` (section 2).

There is no existing zoom or scale of the whole window set. `stripviewanimator` and
`striptransitionmanager` animate strip PANNING, not a zoomed-out scene.

---

## 6. Shortcut catalog — adding a verb like ToggleOverview

End to end, in the order the existing workspace verbs took:

1. Shortcut id — add to `src/daemon/controllers/shortcutmanager_ids.h` beside :42-51. If it should
   follow the workspaces feature gate, add it to the enumerated `kWorkspaceIds` set at
   `shortcutmanager.cpp:62-67`; that set drives both `isWorkspaceShortcutId` and the KGlobalAccel
   grab gate at :601-618. Do NOT switch to prefix matching — the enumeration is deliberate
   (comment :54-59).
2. Config key plus default — group accessor in `src/config/configkeys_workspaces.h`; default
   accessor in `src/config/configdefaults_workspaces.h`. Shortcut chords themselves are
   `Shortcuts.Global` keys, not `Workspaces.*`.
3. Settings plumbing (per CLAUDE.md's four-step recipe) — signal in
   `src/core/interfaces/isettings.h`; `Q_PROPERTY` plus getter/setter/member in
   `src/config/settings.h`; setter in the matching `src/config/settings/*.cpp` by concern;
   load/save/reset arms in `src/config/settings/loadsave.cpp`.
4. Registration — an entry in `kStaticEntries[]` in `shortcutmanager.cpp` (~:49 onward) with
   `id`, `description` via `PhosphorI18n::tr()`, and a sequence lambda reading the settings
   accessor (`parseSequence(s->...Shortcut(), idCopy)` pattern at :705/:728), emitting a new
   `ShortcutManager` signal.
5. Daemon wiring — connect that signal in `src/daemon/daemon/workspaces.cpp` beside the existing
   verb connects at :641-712 (or `src/daemon/daemon/shortcuts_wiring.cpp` for non-workspace verbs).
6. Effect handler — signal in `dbus/org.plasmazones.WindowTracking.xml`, declaration in
   `windowtrackingadaptor.h` near :1278, subscription in
   `kwin-effect/plasmazoneseffect/daemon_bringup.cpp` beside :835, `slot*` handler in
   `kwin-effect/plasmazoneseffect/daemon_apply.cpp`.

Dynamic (non-static) shortcut binds use `persistent=false` in the quick-layout-slot style; the
prefix-matched dynamic id list is `shortcutmanager.cpp:409-412`
(`kSnapToZonePrefix`, `kWorkspaceMoveSlotPrefix`, `kWorkspaceFocusSlotPrefix`,
`kWorkspaceNamedFocusPrefix`, `kWorkspaceNamedMovePrefix`).

Cheatsheet: `src/daemon/overlayservice/cheatsheet.cpp:5` — shortcuts are "grouped by category
and filtered by the tiling mode of the screen". A mode-neutral workspace or overview verb needs a
deliberate placement in that grouping (the workspace family is already mode-neutral, per
`configkeys_workspaces.h:22-25`). Daemon side also `src/daemon/daemon/cheatsheet.cpp`; QML
`src/ui/CheatsheetContent.qml` plus `CheatsheetGroup.qml` and `CheatsheetRow.qml`.

---

## 7. Settings

### Page registration

`src/settings/controller/settingscontroller_pageregistration.cpp`:
- comment :110 — "Dynamic per-monitor workspaces: a top-level DRILL-IN parent like the ..."
- `regVirtual("workspaces", "", tr("Workspaces"), ...)` :120 — the parent
- `regVirtual("workspaces-behavior", "workspaces", tr("Behavior"),
  "pages/workspaces/WorkspacesBehaviorPage.qml", "configure", ...)` :123-125
- `regVirtual("workspaces-named", "workspaces", tr("Named Workspaces"),
  "pages/workspaces/NamedWorkspacesPage.qml", "bookmark", ...)` :127-129
- `regVirtual("workspaces-shortcuts", "workspaces", tr("Quick Shortcuts"),
  "pages/workspaces/WorkspacesShortcutsPage.qml", ...)` :131-133

An Overview leaf is a fourth `regVirtual` under the same parent. Watch the pre-existing
`"overview"` page id collision noted in the plan (line 59) and visible around
`settingscontroller_pageregistration.cpp:87`; `virtualscreens` (:106) is the AdvancedOnly model.

QML: `src/settings/qml/pages/workspaces/` — `WorkspacesBehaviorPage.qml`,
`NamedWorkspacesPage.qml`, `NamedWorkspaceRow.qml`, `WorkspacesShortcutsPage.qml`.
(The plan's `WorkspacesPage.qml`, `NamedWorkspacesCard.qml` and `WorkspaceShortcutsCard.qml` do not
exist — drift note, plan lines 26-31.)

### Config group naming

`src/config/configkeys_workspaces.h`, class `ConfigKeysWorkspaces : public ConfigKeysScrolling` :27.

Groups :34-40:
- `workspacesGroup` = `"Workspaces"` :37 — a container only; exists so `reset()` can drop the whole
  subtree in one `deleteGroup`, like the `Snapping` and `Shortcuts` rows
- `workspacesBehaviorGroup` = `"Workspaces.Behavior"` :38
- `workspacesNamedGroup` = `"Workspaces.Named"` :39
- `workspacesSlotsGroup` = `"Workspaces.Slots"` :40

Rationale recorded :22-25: dynamic workspaces are mode-neutral, a layer BELOW all three placement
modes, so the groups sit at the top level rather than under `Snapping.*`, `Tiling.*` or
`Scrolling.*`. An Overview settings group follows the same reasoning.

Constants:
- `WorkspaceSlotCount = 9` :52 — the single spelling of "nine" behind the `Shortcuts.Global`
  `WorkspaceMoveSlotN` / `WorkspaceFocusSlotN` chords, the `Workspaces.Slots` `TargetN` entries,
  the key-builder range guards, the schema declaration loops and Settings' index bounds
- `WorkspaceNameMaxLength = 64` :61 — capped because the daemon hands the name to KGlobalAccel as
  an ad-hoc action's objectName and description, written verbatim into `kglobalshortcutsrc` (an INI)

Behavior keys :63-73: `ManageKWinPerOutput` :69, `SnapBackOsdHint` :71,
`RebindKWinDesktopShortcuts` :73. Named key :79-80 — a JSON array of
`{name, output, position, focusShortcut, moveShortcut}` maps.

Cap badge plumbing an Overview page could reuse: `settingscontroller.h:346`,
`settingscontroller_pagekeys.cpp:74` (`m_workspaceVdm->desktopCount() >= DefaultDesktopCap`)
and `:85`.

Page-owned key manifests are validated by `tests/unit/settings/test_page_owned_config_keys.cpp`.

---

## 8. Test infrastructure

### Library tests (LGPL — a library's own tests follow the library)

`libs/phosphor-workspaces/tests/`:
- `CMakeLists.txt`
- `test_workspace_map.cpp` — map invariants, position arithmetic, serialization round-trip
- `test_workspace_reconciler.cpp` — scripted notification sequences
- `test_workspace_ledger.cpp` — echo-ledger matching and expiry
- `test_virtualdesktopmanager.cpp`
- `workspacereconcilerharness.h` — the shared driver; new reconciler behavior belongs here,
  since the reconciler's inputs are plain calls with no D-Bus

### Top-level tests (GPL), `tests/unit/`

Subdirectories: `autotile`, `compositor-common`, `config`, `core`, `daemon`, `dbus`, `editor`,
`helpers`, `rendering`, `scripting`, `scrolling`, `settings`, `shadervalidate`, `shell`, `snap`,
`ui`. Registered from `tests/unit/CMakeLists.txt`.

Workspace-touching files that already exist:
- `tests/unit/autotile/engine/test_autotile_engine_workspaces.cpp`
- `tests/unit/core/snap/test_snap_engine_workspaces.cpp`
- `tests/unit/core/screens/test_virtual_desktop_per_screen.cpp` (per-screen mode inference; the
  gate test is at :91)
- `tests/unit/daemon/test_shortcutmanager_teardown.cpp`
- `tests/unit/settings/test_page_owned_config_keys.cpp`
- `tests/unit/core/layout/test_layoutmanager_assignment.cpp` (mentions workspaces)
- `tests/unit/shell/test_bar_controller.cpp` and
  `tests/unit/settings/pages/test_animations_page_controller.cpp` (incidental mentions)

Gap: there is no `test_scroll_engine_workspaces.cpp`. Autotile and snap each have a workspaces
engine test; scrolling does not. That asymmetry is worth naming in an overview plan, since the
scroll engine has the largest per-desktop state surface (plan section 5, lines 399-402).

Build and test invocation (CLAUDE.md): `cmake -B build -DBUILD_TESTING=ON -DBUILD_PHOSPHOR_SHELL=ON`,
then `ctest --test-dir build --output-on-failure`. `BUILD_TESTING` defaults OFF, and a build dir
configured without it reports "No tests were found", which reads like success.
`BUILD_PHOSPHOR_SHELL` gates the whole shell tier. `glslangValidator` or `glslang` must be on PATH
or the `shader_validate_animations` gate hard-fails.

---

## Two things worth flagging for the plan

1. The data feed exists and is unclaimed; the rendering surface does not. The effect already
   caches the workspace map with generation and epoch ordering
   (`daemon_bringup.cpp:982-1015`, `plasmazoneseffect.h:3614-3623`) and reads nothing from it.
   But there is no `QuickSceneEffect`, no `setActiveFullScreenEffect` claim, and no gesture
   registration anywhere in `kwin-effect/`, `src/` or `libs/`. An in-compositor overview
   introduces all three. The alternative precedent, a daemon-hosted layer-shell Overlay surface
   driven by `phosphor-layer` plus dma-buf window thumbnails from
   `SnapAssistThumbnailCapture`, is fully built and in production for snap assist.

2. No per-desktop strip query. `visibleStripJson` gates on
   `m_engine->isActiveOnScreen(screenId)` (`scrollingadaptor.cpp:432`) and answers only the
   screen's CURRENT desktop and activity, a limitation `ScrollEngine.h:547` records explicitly.
   Rendering every workspace's strip in one zoomed-out view has nothing behind it today, even
   though the per-context state survives in `PerScreenStates` keyed by
   `PlacementStateKey{screenId, desktop, activity}` (`EngineTypes.h:21`).
