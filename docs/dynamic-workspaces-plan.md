<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# Implementation plan — Dynamic per-monitor workspaces (v3.5)

> **This is a historical design record, not current documentation. Superseded by the
> shipped implementation (PR #990, v3.5.0), banner dated 2026-08-29.** It is written in
> the imperative because it was a plan. Read it as "what we intended to build", never as
> a description of the tree, and check the code before relying on any name, key or
> default below. The drift list that follows is what was found when the plan was
> re-checked against the shipped code; it is not guaranteed exhaustive. For what the
> feature actually does, see `docs/dynamic-workspaces.md` and the code.

Planned 2026-08-26 against the tree as it then stood. Every seam in the planning prompt
(§5) was re-read; drift found is recorded inline and in the risk register. The planning
prompt this plan was the deliverable of has since been deleted, along with the
implementation prompt, both being agent-driving scaffolding rather than documentation.
References to them below are historical.

> **Implementation drift (post-ship note).** Some names, keys and defaults below differ
> from what shipped.
>
> - The shortcut key defaults planned as a separate
>   `src/config/configdefaults_workspace_shortcuts.h` landed inside
>   `src/config/configdefaults_workspaces.h`. No file of the planned name exists.
> - The single settings page planned as
>   `src/settings/qml/pages/workspaces/WorkspacesPage.qml` with `NamedWorkspacesCard.qml`
>   and `WorkspaceShortcutsCard.qml` shipped instead as three pages plus a row delegate in
>   the same directory: `WorkspacesBehaviorPage.qml`, `NamedWorkspacesPage.qml` (with
>   `NamedWorkspaceRow.qml`), and `WorkspacesShortcutsPage.qml`. None of the three planned
>   names exists.
> - The config key table below writes leaf key names in lowerCamelCase. The shipped keys
>   are PascalCase, and two of them were renamed: `Enabled`, `ManageKWinPerOutput`,
>   `SnapBackOsdHint`, `RebindKWinDesktopShortcuts` and `Entries`. The accessors are in
>   `src/config/configkeys_workspaces.h`, which is authoritative.
> - A third group shipped that the plan never names: `Workspaces.Slots`, holding one
>   `Target%1` key per quick slot (the named workspace that slot points at). The slots'
>   own chords are `Shortcuts.Global` keys, `WorkspaceFocusSlot%1` and
>   `WorkspaceMoveSlot%1`, so each slot carries two chords rather than one.
> - The shortcut defaults below no longer match. Both the focus-slot and the move-slot
>   chords ship unbound, and the per-slot move defaults the plan assigned were removed in
>   favour of a single shared default.

## 0. Seam re-verification results (drift vs the prompt)

Confirmed as described: the 5-connection `desktopChanged` block in
`kwin-effect/plasmazoneseffect/lifecycle_wiring.cpp` (378/395/407/438/499); the effect
report path `PlasmaZonesEffect::reportScreenDesktop()` (`plasmazoneseffect/screens.cpp:105`)
with dedup + bringup re-sync; `WindowTrackingAdaptor::screenDesktopChanged`
(`src/dbus/windowtrackingadaptor/lifecycle.cpp:906`); the `[SEQ A]–[SEQ E]` fan-out lambda
(`src/daemon/daemon/start.cpp:335+`); `Daemon::currentDesktopForScreen`
(`src/daemon/daemon/osd.cpp:1254`) and the injected-provider adapters in
`src/daemon/controllers/contextresolverwiring.cpp`; the `scrollTabStripsChanged` +
`scrollTabStrips` replay pattern (`src/dbus/tilingadaptor/tilingadaptor.h:117,308,541`,
`kwin-effect/tilinghandler/wiring.cpp:75,127,612-645` — note the replay side also has a
query-generation counter and bounded retry, both worth copying);
`kStaticEntries[]` in `src/daemon/controllers/shortcutmanager.cpp` (~line 49); the settings
page registration seams (`settingscontroller_pageregistration.cpp`, `"overview"` collision
at line 87, `virtualscreens` at 106); the inferred `perScreenModeActive()` and its test
(`tests/unit/core/screens/test_virtual_desktop_per_screen.cpp:91`);
`PlacementStateKey{screenId, int desktop, activity}` in
`libs/phosphor-engine/include/PhosphorEngine/EngineTypes.h:21` with the shared
`PerScreenStates` container (`PerScreenStates.h` — has `takeState`/`insertState` and
`removeStatesIf(pred)` at line 179, the natural reap/re-key primitives).

**Drift found:**

1. **`VirtualDesktopManager` has NO `createDesktop` / `removeDesktop` / `setDesktopName`
   methods at all** (`libs/phosphor-workspaces/include/PhosphorWorkspaces/VirtualDesktopManager.h`,
   95 lines). The prompt reads as if they exist uncalled; they must be added. KWin's D-Bus
   interface (`org.kde.KWin.VirtualDesktopManager`) does expose `createDesktop(uint
   position, QString name)`, `removeDesktop(QString id)`, `setDesktopName(QString id,
   QString name)` — the wrappers are thin, but they are new code (Phase 1).
2. **The daemon→effect window-move command already exists end to end.** The prompt asks us
   to design it; we only extend it. `PlacementEngineBase::windowDesktopMoveRequested
   (windowId, desktop)` → forwarded by `WindowTrackingAdaptor` (`enginewiring.cpp:370-380`,
   emitted directly from `crossmode.cpp:218` and `rules_placement.cpp:224`) → effect
   `slotWindowDesktopMoveRequested` (`plasmazoneseffect/daemon_apply.cpp:70`) which calls
   `KWin::effects->windowToDesktops(w, {all.at(desktop-1)})`, refusing sticky windows and
   out-of-range indices. The cross-screen half also exists:
   `slotWindowOutputMoveExpected(windowId, targetScreenId, sourceScreenId)` hands the
   one-shot to the tiling handler's cross-output transfer path (the scrolling
   monitor-crossing verbs ride it today). Both are reused as-is; §4.2 below only adds a
   desktop-id-addressed variant.
3. **No runtime-state-file precedent exists.** Nothing under `src/` or `libs/` persists
   runtime state outside config, caches (`CacheLocation`), and user data assets
   (`GenericDataLocation`). The reuse mandate's "follow the existing state persistence"
   resolves to "there is none"; §3.3 picks `QStandardPaths::StateLocation`
   (`~/.local/state/plasmazones/plasmazonesd/`) and a JSON format mirroring the wire format, which is
   the closest thing to a house style (JSON everywhere else).
4. `ActivityManager` lives beside `VirtualDesktopManager` in phosphor-workspaces; the new
   classes join that pair. Current file sizes (95 h / 399 cpp) leave headroom, but the
   ownership map is still its own class per fork 1 and the ceiling rule.

## 1. Architecture overview

Three new cooperating pieces, split along the existing library/daemon/effect lines:

- **`PhosphorWorkspaces::WorkspaceMap`** (LGPL, phosphor-workspaces) — the pure data model:
  `screenId → ordered QStringList of KWin desktop UUIDs`, plus per-desktop metadata
  (named-workspace name, home output). No D-Bus, no Qt signals beyond `changed()`;
  fully unit-testable. It is the single authority for ownership; serialization to/from the
  wire/state JSON lives here so daemon, tests, and (deserialization only) the effect agree
  on one format.
- **`PhosphorWorkspaces::WorkspaceReconciler`** (LGPL, phosphor-workspaces) — the lifecycle
  state machine (§4). Owns the pending-op ledger for echo safety, computes the ordered
  KWin D-Bus operations for every lifecycle event, and mutates the `WorkspaceMap`. It is
  driven by, and calls back into, `VirtualDesktopManager` (extended with the create/remove/
  rename wrappers). It has no daemon dependencies — inputs are plain notifications
  (desktop list replies, per-screen desktop reports, window population counts) so the unit
  tests can drive it without D-Bus.
- **`WorkspaceController`** (GPL, `src/daemon/controllers/`) — daemon glue: wires
  VirtualDesktopManager/effect signals into the reconciler, owns the state file, the
  settings gate, OSD hints, verb resolution (shortcut → target desktop UUID → engine/effect
  command), and the change-gated D-Bus stream to the effect.

The daemon is the sole writer of the map (fork 1). The effect only consumes the streamed
map (for the future overview and for nothing else in this plan) and continues to report
per-screen desktops exactly as today. Window population ("is desktop D empty") is answered
from the daemon's existing window tracking (`WindowTrackingAdaptor` / window service window
list with per-window desktop), not by a new census — see §4.1.

### Id-vs-index policy per layer (the §5.2 gap)

| Layer | Keying | Rationale |
|---|---|---|
| WorkspaceMap, state file, wire JSON, named-workspace config | KWin UUID strings | Stability across renumbering is the whole point |
| VirtualDesktopManager | Both: keeps `m_desktopIds` (already present) and grows public `desktopIdAt(int)` / `desktopIndexOf(id)` translation | It already holds the authoritative ordered id list from `applyDesktopListReply()` |
| Engines (`PlacementStateKey`), effect reports, `screenDesktopChanged` fan-out, existing settings/rules | 1-based int, unchanged | Rewriting every consumer to UUIDs is a repo-wide churn with no behavioral gain; instead the daemon re-keys engine state on renumber (§5) and translates at the chokepoints |

Translation happens at exactly two chokepoints: `Daemon::currentDesktopForScreen` (int for
legacy consumers) and the new `WorkspaceController` (uuid↔int both ways via
`VirtualDesktopManager`). No third authority (reuse mandate: the injected-provider pattern
at `contextresolverwiring.cpp` is extended, not duplicated).

## 2. Phases

Per fork 6. Each phase builds, tests green, and is usable with plain shortcuts before the
next starts.

- **Phase 1 — Model + stream.** WorkspaceMap, WorkspaceReconciler, VirtualDesktopManager
  create/remove/rename wrappers + id translation, WorkspaceController skeleton, the enable
  gate (setting + KWin mode detection), first-run adoption, create-on-occupy /
  destroy-on-empty / trailing-empty, engine reap + re-key, the daemon→effect map stream +
  replay query. Cut line: dynamic lifecycle works end to end driven only by existing window
  movement (Pager, KWin shortcuts still stock); no new verbs yet.
- **Phase 2 — Verbs + shortcuts.** All §2 verbs, ShortcutManager entries, OSD feedback,
  owner-wins snap-back (needs the verbs' switch path). Cut line: full keyboard workflow on
  a static monitor set within one daemon run.
- **Phase 3 — Named workspaces.** Config schema, creation at startup, destroy-on-empty
  exemption, named shortcut targets, output pinning, settings list editor + dynamic
  shortcut rows. Cut line: named workspaces fully usable; hotplug still naive.
- **Phase 4 — Hotplug + persistence.** Slice migration on output remove/add, home-output
  memory, state file save/restore, daemon-restart re-adoption. Cut line: feature survives
  dock/undock and reboot.
- **Phase 5 — Stock-shortcut handling + polish.** KWin desktop-switch shortcut
  neutralization/rebind while enabled, external-count-change collision policy hardening,
  desktop-cap degradation, docs (Pager unsupported-by-policy note), settings polish.

## 3. Data model, wire format, state file

### 3.1 WorkspaceMap structure

```
struct WorkspaceEntry {            // one per owned desktop
    QString desktopId;             // KWin UUID (braced QUuid string as KWin reports it)
    QString name;                  // empty for dynamic; non-empty == named (exempt)
    QString homeScreenId;          // set only while displaced by hotplug (Phase 4)
};
class WorkspaceMap {
    // screen order is the slice-concatenation order (fork 5): sorted by output
    // geometry left-to-right, ties by screenId; recomputed on screen add/remove.
    QStringList m_screenOrder;
    QHash<QString, QList<WorkspaceEntry>> m_slices;   // screenId → ordered slice
    QHash<QString, QString> m_ownerOf;                // desktopId → screenId (index)
};
```

Invariants enforced by the class (asserted in debug, repaired + logged in release):
every KWin desktop id appears in exactly one slice; `m_ownerOf` is the inverse of the
slices; concatenating slices in `m_screenOrder` yields exactly
`VirtualDesktopManager::m_desktopIds` order (fork 5 contiguity). The class exposes
`globalPositionForInsert(screenId, sliceIndex)` — the position arithmetic for
`createDesktop(position, name)`: sum of slice lengths of preceding screens + sliceIndex
(uint, 0-based per KWin's D-Bus signature; verify KWin's position base during Phase 1
bringup and encapsulate it here only).

### 3.2 Wire format (daemon→effect stream and replay)

One JSON object as a QString payload, `scrollTabStripsChanged`-style (fork 4, §5.5), on a
new adaptor signal `WindowTrackingAdaptor::workspaceMapChanged(QString mapJson)` with
replay query `workspaceMap()` (WindowTracking is where the desktop flows already live;
Tiling would be a category error). Change-gated: the controller caches the last serialized
payload and emits only on byte difference.

```json
{
  "v": 1,
  "generation": 421,
  "screenOrder": ["DP-1", "DP-2"],
  "slices": {
    "DP-1": [
      {"id": "{uuid-a}", "index": 1, "name": "chat", "current": true},
      {"id": "{uuid-b}", "index": 2}
    ],
    "DP-2": [ {"id": "{uuid-c}", "index": 3, "current": true} ]
  }
}
```

`index` is the live 1-based global int for the effect's convenience (it works in
`effects->desktops()` order); `id` is authoritative. `generation` is the reconciler's
monotonic map generation — the overview will use it to discard stale replays exactly as
`m_scrollTabStripsQueryGeneration` does in `tilinghandler/wiring.cpp:624-641`. Trailing
empty and displaced-home metadata are deliberately omitted from v1 of the wire format; the
overview needs geometry-free identity + order + current, nothing more (add fields, never
reinterpret, if it turns out otherwise).

### 3.3 State file (Phase 4)

`QStandardPaths::writableLocation(QStandardPaths::StateLocation)` →
`~/.local/state/plasmazones/plasmazonesd/workspaces.json`. NOT config.json (prompt requirement) and not
GenericDataLocation (that tree is user-visible assets). Format = the wire object plus
`"homeScreen"` per displaced entry and per-desktop `"name"` snapshots, written atomically
(QSaveFile) on map change, debounced ~1s. Restore semantics in §4.6. No migration
machinery: `"v"` mismatch or parse failure ⇒ discard and re-adopt (state is reconstructible
by definition; the no-ad-hoc-migration policy applies in spirit).

Named-workspace *declarations* are config (§7), never in the state file; the state file
records only which UUID currently realizes each declared name.

## 4. Reconciliation state machine and lifecycle algorithms

### 4.1 Inputs, outputs, echo safety

Inputs to the reconciler: (a) `applyDesktopListReply` deltas from VirtualDesktopManager
(authoritative id list — created/removed/renamed/reordered); (b) per-screen current-desktop
reports (the existing effect path); (c) window population deltas — the controller
subscribes to the daemon's existing window add/remove/desktop-move tracking and maintains
`desktopId → window count` (translating the tracked int desktop through
VirtualDesktopManager at event time; sticky/on-all-desktops windows count for no desktop);
(d) screen add/remove; (e) verb requests; (f) settings changes.

Outputs: KWin D-Bus calls (`createDesktop`, `removeDesktop`, `setDesktopName`,
`setCurrent`), map mutations (→ stream + state file), engine re-key/reap directives,
effect window-move commands, OSD hints.

**Echo safety — pending-op ledger.** Every KWin call we issue is recorded before the call:
`{opId, kind, expected outcome, deadline}` — e.g. `Create{position, name}` expects a
desktopCreated with a new id at that position; `Remove{id}` expects that id to vanish;
`SetCurrent{screenId, id}` expects a `screenDesktopChanged(screenId, index-of(id))` report.
When a notification arrives, the reconciler first tries to match and retire a ledger entry;
matched notifications update the map/bookkeeping but trigger NO reactive policy (no
snap-back, no adoption, no trailing-empty response beyond what the op itself planned).
Unmatched notifications are external and get the full policy response. Entries expire on a
deadline (2s) with a logged warning and a full resync from the `desktops` property — the
same self-healing shape as the existing bringup re-sync. `setCurrent` echoes additionally
carry a per-screen suppression token so the snap-back path (§4.7) can never respond to its
own correction: snap-back issues at most one corrective `SetCurrent` per external switch
event, and a second foreign report for the same screen while that ledger entry is open is
queued, not acted on — this breaks any loop against a re-asserting Pager/KWin.

### 4.2 Verb execution primitives (reused, per §5.4 verification)

- Window→desktop: emit the existing `WindowTrackingAdaptor::windowDesktopMoveRequested
  (windowId, int desktop)` with the target's *current* int index resolved at emit time.
  Because the effect resolves `all.at(desktop-1)` against live KWin order, the
  UUID→int translation must happen after any pending create/remove ops retire (the
  controller serializes verb execution behind the ledger: a verb that depends on a pending
  op waits for its retirement). Failure mode when the effect is not loaded: fireAndForget
  is one-way, so the daemon watches for the expected population delta; on timeout it drops
  the op, logs, and shows no OSD success. No new marshalled type.
- Window→desktop on another screen: same signal, preceded by
  `slotWindowOutputMoveExpected` marking via the existing adaptor path (the scrolling
  monitor-crossing verbs' mechanism, reused verbatim).
- Column moves: the scroll engine's existing column-move machinery re-targeted — the
  controller asks ScrollEngine for the focused column's window ids and issues the window
  primitive per member, with the engine's existing batch/continuity handling (Phase 2
  detail: reuse the monitor-crossing column path, which already does exactly this for
  screens; the desktop variant differs only in also emitting desktop moves).

### 4.3 Core lifecycle algorithms (each a precise KWin-call sequence)

**Create-on-occupy** (a window lands on screen S's trailing empty desktop T):
1. Ledger `Create{pos = globalPositionForInsert(S, slice(S).size()), name=""}`.
2. `createDesktop(pos, "")` via VirtualDesktopManager.
3. On matched desktopCreated + list reply: append new id to slice(S), bump generation,
   stream + persist. Engines need nothing for T (its state exists) but every desktop whose
   global int shifted gets the re-key pass (§5).

**Destroy-on-empty** (last window leaves non-named, non-trailing desktop D on screen S):
1. Debounce (~300ms) — window moves between desktops produce leave+arrive pairs.
2. Last-moment emptiness re-check against the population map; if occupied, abort.
3. Ledger `Remove{D}` → `removeDesktop(D)`.
4. **Adopt-if-lost race arm:** if, between issuing and the desktopRemoved echo, a window
   population event arrives for D, we cannot un-issue; KWin moves that desktop's windows to
   a neighbor on removal. The reconciler marks those window ids "displaced by op"; when
   their arrival reports land on the neighbor desktop, the controller re-issues a window
   move onto S's current desktop (owner-wins applied to our own casualty) with an OSD hint.
5. On matched removal + list reply: drop from slice, re-key pass for shifted ints, reap
   D's engine state (§5), stream + persist. `clampScreenDesktopsToCount()` already keeps
   the per-screen int map sane in the interim (verified in VirtualDesktopManager.h:75-78);
   the ledger treats the clamped reports during the window as echoes, not external
   switches.

**Trailing-empty maintenance:** after every map/population change, per screen: if the last
slice entry is occupied → create-on-occupy; if the slice has ≥2 trailing empties (can
happen after external ops) → destroy the surplus from the end (named exempt); a slice may
never become empty — a screen always retains ≥1 desktop.

**First-run adoption** (feature enabled, or daemon start with no/invalid state file):
1. Read `desktops` property; read each screen's current desktop (effect reports; bringup
   re-sync guarantees these arrive — gate adoption until every known screen has reported,
   with a timeout fallback to the global current).
2. Ownership: each desktop currently shown by a screen → that screen; ties (two screens on
   one desktop) → the first in screen order (leftmost/primary). All remaining desktops →
   distributed to keep KWin's existing order contiguous per fork 5: walk the global list
   and assign each unowned desktop to the owner of the nearest preceding owned desktop
   (first segment → first screen).
3. Re-sort KWin's global order to match slice concatenation if it does not already: KWin's
   D-Bus interface has no reorder verb, so contiguity is achieved by remove+recreate ONLY
   for empty desktops; occupied out-of-place desktops are left where they are and the map
   tolerates temporary non-contiguity (weakened fork-5 invariant: contiguity is restored
   opportunistically as desktops empty; the position arithmetic always inserts
   slice-correctly so drift never grows). This is a refinement, not a re-open: forced
   window-preserving reorder does not exist in the API.
4. Trailing-empty pass; named-workspace creation pass (Phase 3); stream + persist.

**External creation** (unmatched desktopCreated): adopt by the currently focused screen
(fork 3), appended before that screen's trailing empty; re-key pass; stream.

**External count changes** (System Settings spinner bulk add/remove — arrives as N
unmatched created/removed events plus a rows/count change): *collision policy:* additions
adopt as above; an unmatched removal of an owned desktop is accepted as fact (KWin is the
source of truth) — drop it from its slice, reap/re-key, and run trailing-empty maintenance
to restore each screen's invariant. If removal leaves a screen's slice empty, immediately
create one (its screen must never have zero). No attempt to fight the spinner; the map
follows, invariants repair.

**KWin maximum desktop count:** KWin's cap (`VirtualDesktopManager::maximum()`, 20 in
current KWin — re-verify the constant during Phase 1 against the running compositor by
probing createDesktop failure) bounds Σ slices. Degradation: when count == cap, the
trailing-empty rule is suspended globally (occupying the last empty does not append); the
OSD shows a one-time "workspace limit reached" hint; destroy-on-empty naturally reopens
headroom. Named-workspace creation past the cap fails with a logged warning and a settings
page badge, never a crash.

### 4.4 Hotplug migration (Phase 4)

Output removed: its slice entries get `homeScreenId = removed screen`, and the slice is
appended to the fallback screen's slice (screen-order neighbor, else primary) *before* its
trailing empty. No windows move (KWin already re-homes the windows with the output; our map
just re-owns the desktops). Engines: the daemon's existing screenRemoved path plus a re-key
pass mapping `(oldScreen, d, a) → (fallbackScreen, d′, a)` for affected desktops — but
note the engines already handle output-loss via their own screen-set machinery; the re-key
only covers the per-desktop dimension (§5). Output (re)added: entries whose
`homeScreenId` matches migrate back (same order), `homeScreenId` cleared; trailing-empty
maintenance runs on both screens. Home matching is by screenId (the effect's stable
duplicate-aware id from `screens.cpp` — verified it disambiguates twin models with the
`baseId/connector` scheme).

### 4.5 Renumbering window

Between `removeDesktop` and the next list reply, ints are stale. Mitigations: the ledger
serializes our own ops (one structural op in flight per screen); verb translation waits for
ledger quiet; `clampScreenDesktopsToCount` covers the per-screen current map; the re-key
pass runs only on the settled list reply, computing old→new int mapping from the id list
delta (ids are the fixed points).

### 4.6 Daemon restart + state restore ordering

On start: load state file → hold it as *candidate* map → wait for the KWin `desktops`
reply and the effect bringup re-sync (`daemon_bringup.cpp:255-270` re-pushes every screen's
desktop after service registration — the ordering anchor). Reconcile candidate vs reality:
ids present in both keep their recorded owner/name/home; ids only in reality are adopted
(fork 3); ids only in the candidate are dropped. Then invariant repair + stream. The replay
query means a late-loading effect always converges regardless of which side restarts first
(same contract as `scrollTabStrips`).

### 4.7 Owner-wins snap-back

An unmatched `screenDesktopChanged(S, n)` where `desktopIdAt(n)` is owned by screen O ≠ S:
1. Issue ledgered `SetCurrent` restoring S to its previous slice desktop (per-screen
   suppression token, §4.1 — one correction per external event, further reports queue).
2. OSD hint on S ("That workspace lives on another monitor" — plain prose, via the
   existing OSD service; toggleable, §7).
Window arriving on a foreign-owned desktop (unmatched population event): the window
follows the desktop — no action needed beyond engines (the desktop's owner screen context
already covers it); if the window is *visible on S* but the desktop belongs to O, the
controller issues the §4.2 cross-screen move so window and desktop reunite on O.
Stock KWin walk-through shortcuts: handled by rebinding (Phase 5, §7) so this path is the
exception, not the steady state.

## 5. Engine impact (all three modes)

Enumerated per-desktop state (the int-keyed corruption surface):

- **Scrolling** — `ScrollEngine`: `PerScreenStates<ScrollState>` (one per key), window→key
  map, sticky-pin map in `ScreenContextTracker` (pin stores an int desktop), drag-insert
  preview context. Largest surface; the sticky-pin divergence
  (`ScreenContextTracker.h:180-200`) means the pin must be re-keyed too, not just the
  state maps.
- **Tiling** — autotile engine: `PerScreenStates<TilingState>` (`TilingStateKey` alias),
  window→key map, sticky pins (same tracker base).
- **Snapping** — snap engine: per-key snap stores (per-screen snap architecture), resnap
  bookkeeping in `phosphor-placement/src/resnap.cpp`, same tracker.

**Mechanism — one shared pass, placed in phosphor-engine** (reuse mandate): a new
`PhosphorEngine::PlacementEngineBase` (or `ScreenContextTracker`-adjacent) pair of virtuals
with a shared default implementation over `PerScreenStates`:

- `reapDesktopState(int desktop)` — built on `removeStatesIf` (exists,
  `PerScreenStates.h:179`) + window-key map cleanup + pin cleanup. Called by the daemon
  fan-out when a desktop is destroyed, before the re-key pass.
- `renumberDesktopState(const QHash<int,int>& oldToNew)` — `takeState`/`insertState`
  rewrite of every key + window-key map + pins. Entries whose old int is absent from the
  mapping are reaped (they belonged to the removed desktop).
- Hotplug slice migration composes the same primitive with a screenId rewrite.

Each engine overrides only if it has extra maps outside the shared containers (audit each
during Phase 1 implementation; scrolling's drag-insert preview is cancelled, not migrated —
the `[SEQ A]` precedent). Per-mode statement: all three engines take the identical shared
arm; no mode needs a bespoke behavior (the float-is-per-mode invariant is untouched — keys
move, per-mode float slots move with their state objects). The daemon fan-out site is the
existing `screenDesktopChanged` lambda's file (`start.cpp`), a sibling lambda driven by the
WorkspaceController.

When the feature is **off**, none of this runs: no reap, no re-key, no map — current
behavior byte-identical (the gate wraps the WorkspaceController's construction, not
scattered ifs).

## 6. File-level change list per phase

Licenses: `libs/phosphor-*` = LGPL-2.1-or-later; daemon/effect/settings = GPL-3.0-or-later.
All new files justified against §7 reuse: WorkspaceMap/Reconciler have no existing
analogue (first ownership model in the repo); everything else extends existing files.

**Phase 1**
- NEW `libs/phosphor-workspaces/include/PhosphorWorkspaces/WorkspaceMap.h` + `src/WorkspaceMap.cpp` (LGPL) — model + JSON (de)serialization.
- NEW `libs/phosphor-workspaces/include/PhosphorWorkspaces/WorkspaceReconciler.h` + `src/WorkspaceReconciler.cpp` (LGPL) — state machine + ledger. (Split ledger into `WorkspaceLedger.h/.cpp` up front if the reconciler nears the ceiling.)
- MOD `libs/phosphor-workspaces/{include/PhosphorWorkspaces/VirtualDesktopManager.h,src/VirtualDesktopManager.cpp}` — add `createDesktop(uint pos, QString name)`, `removeDesktop(id)`, `setDesktopName(id, name)` D-Bus wrappers; `desktopIdAt`/`desktopIndexOf`/`desktopIds()` accessors; surface created/removed deltas with ids (today the slots refetch without exposing which id changed).
- MOD `libs/phosphor-workspaces/CMakeLists.txt`.
- NEW `src/daemon/controllers/workspacecontroller.h/.cpp` (GPL) — glue per §1 (split `workspacecontroller_verbs.cpp` in Phase 2 to stay under the ceiling).
- MOD `src/daemon/daemon/start.cpp` (+ the daemon header) — construct/wire behind the gate; sibling fan-out lambda for reap/re-key.
- MOD `libs/phosphor-engine/include/PhosphorEngine/PerScreenStates.h` (or new `DesktopRekey.h` if header size demands) — shared reap/renumber pass.
- MOD engine sources that own extra per-desktop maps (audit: `libs/phosphor-scroll-engine/src/…`, snap engine, autotile engine, `ScreenContextTracker`).
- MOD `src/dbus/windowtrackingadaptor/…` — `workspaceMapChanged(QString)` signal + `workspaceMap()` replay method + XML.
- MOD `kwin-effect/plasmazoneseffect/…wiring` — subscribe + cache map (consumer stub for the overview; also the per-output-mode probe below).
- Settings gate keys (subset of §7): MOD `src/config/configdefaults.h`, NEW `src/config/configdefaults_workspaces.h`, MOD `configkeys.h`, `settingsschema*.cpp`, `src/core/interfaces/isettings.h`, `src/config/settings.h`, NEW `src/config/settings/workspaces.cpp`, MOD `src/config/settings/loadsave.cpp`.
- Tests: NEW `libs/phosphor-workspaces/tests/test_workspace_map.cpp`, `test_workspace_reconciler.cpp` (LGPL — library tests follow the library); MOD `tests/unit/core/screens/test_virtual_desktop_per_screen.cpp`.

**Phase 2**
- MOD `src/daemon/controllers/shortcutmanager.cpp` (static entries), NEW `src/config/configdefaults_workspace_shortcuts.h` (mirrors `configdefaults_scrolling_shortcuts.h`), MOD `src/config/settings/shortcuts.cpp`, `src/daemon/daemon/shortcuts_wiring.cpp`.
- NEW `src/daemon/controllers/workspacecontroller_verbs.cpp` — verb resolution; reuses `windowDesktopMoveRequested` / cross-output move / scroll column machinery.
- MOD scroll engine: expose focused-column window enumeration for `move-column-to-workspace` if not already public (audit first — the monitor-crossing verbs suggest it is).
- OSD strings via the existing OSD service (no new surface); `PhosphorI18n::tr()`.

**Phase 3**
- MOD `configdefaults_workspaces.h`/`configkeys.h`/schema/`settings/workspaces.cpp` — named-workspace list key.
- MOD `src/settings/controller/settingscontroller_pageregistration.cpp` — register `"workspaces"` page (id collision with `"overview"` avoided; AdvancedOnly like `virtualscreens`).
- NEW `src/settings/qml/pages/workspaces/WorkspacesPage.qml`, `NamedWorkspacesCard.qml`, `WorkspaceShortcutsCard.qml` (GPL) — patterns per §7; decide PageController vs regVirtual by surveying `src/settings/pages/` (expectation: QML-only regVirtual binding to the settings singleton, like the shortcut pages).
- MOD `WorkspaceReconciler`/`WorkspaceController` — named lifecycle arms; dynamic `persistent=false` shortcut binds in `shortcutmanager.cpp` mirroring the quick-layout slots (`shortcutmanager.cpp:894,1020` region).

**Phase 4**
- MOD `WorkspaceMap`/`Reconciler` (home-output, migration), `workspacecontroller.cpp` (state file, QSaveFile, debounce), daemon screenAdded/Removed wiring.
- Tests: migration + restore units in the phosphor-workspaces test files.

**Phase 5**
- MOD `workspacecontroller.cpp` + settings — stock-shortcut rebind (§7), cap degradation OSD, docs (`docs/` note on the Pager policy; README untouched).

## 7. Settings & shortcuts inventory

### Config keys (group `Workspaces.*` dot-paths, all via new `ConfigDefaults` accessors; sparse persistence applies)

| Key | Default | Notes |
|---|---|---|
| `Workspaces.Behavior` / `enabled` | false | master opt-in |
| `Workspaces.Behavior` / `snapBackOsdHint` | true | §4.7 hint toggle |
| `Workspaces.Behavior` / `manageKWinPerOutputSetting` | false | consent latch for writing kwinrc (below) |
| `Workspaces.Behavior` / `rebindKWinDesktopShortcuts` | true | Phase 5; on = replace stock walk-through while enabled |
| `Workspaces.Named` / `entries` | `[]` | JSON array: `[{"name": "chat", "output": "" \| screenId, "position": 0}]` — name unique non-empty, `output` empty = unpinned, `position` = preferred slice index |
| shortcut keys | see below | one per verb, in `configdefaults_workspace_shortcuts.h` |

New group + defaults ⇒ no migration (latest is `configmigration_v6.cpp`; nothing renamed).

### KWin per-output gate (§5.8 — the inference must not remain the gate)

Layered gate, no single point of trust:
1. **Authoritative probe:** the effect reads the compositor's actual mode (KWin internals
   expose per-output desktops to effects; if no direct API exists, the effect infers from
   `desktopChanged` carrying a non-null `LogicalOutput*`, which only per-output mode
   produces — but probes `kwinrc` reading as the primary) and reports it at bringup via a
   fireAndForget beside the desktop re-sync. Cached daemon-side; replaces
   `perScreenModeActive()` as the feature gate (the inferred method stays for its existing
   callers, untouched).
2. **Config read:** daemon/settings read `kwinrc` `[Windows] PerOutputVirtualDesktops` via
   KConfig (KF6 build) with a QSettings-style fallback for the Qt-only build, as a
   pre-enable check in the settings UI.
3. **Consent write:** enabling the feature with per-output mode off shows an inline
   confirmation on the Workspaces page ("PlasmaZones will turn on KWin's per-output
   virtual desktops setting." — plain prose); on accept, write the key + call KWin's
   `reconfigure()` D-Bus; `manageKWinPerOutputSetting` records the consent. Decline leaves
   the feature off with an explanatory hint. We never write kwinrc silently and never
   revert it on disable (stated in the UI). The setting applies live on `reconfigure()`
   (verified against KWin 6.7.4 source, §11) — the confirmation wording says it takes
   effect immediately; and KWin's own OFF transition collapses all outputs to the active
   output's desktop, giving the feature-disable path a defined end state.

### Settings UI

- **Workspaces page** (`"workspaces"`, top-level, AdvancedOnly like `virtualscreens`):
  enable toggle + consent flow, snap-back hint toggle, rebind toggle, cap notice badge.
- **Named-workspaces list editor** (`NamedWorkspacesCard.qml`): list model bound to the
  `entries` JSON via the settings controller; per-row: name TextField (uniqueness
  validated inline), output ComboBox (existing per-screen picker component — survey
  `src/settings/qml/pages/screens/` for the output-picker in use and reuse it), drag
  reorder, add/remove. Integrates with per-page reset/discard: page registers its keys in
  the `pageOwnedConfigKeys` manifest and the baseline-snapshot machinery exactly as
  existing pages do (find the concrete implementation next to the page being copied —
  memory: `per-page reset/discard` pattern). Edits apply live via normal settings signals;
  the daemon's WorkspaceController reacts (create/rename/unpin) without restart.
- **Shortcuts**: a `WorkspaceShortcutsCard` on the page (matching the per-mode
  QuickShortcutsPage pattern and `ShortcutCaptureField.qml` — no new capture widget), with
  static verb rows plus per-named-workspace rows generated from `entries`, bound
  transiently (`persistent=false`) and re-bound on declaration change, in the
  quick-layout-slot style.

### Shortcuts (all in `kStaticEntries` except the dynamic ones; defaults collision-checked against the current table — no `Meta+Ctrl+Arrow`/`Meta+Shift+Ctrl` desktop chords are used by PZ today — and against KWin stock, which we are rebinding anyway)

| Verb | Proposed default |
|---|---|
| focus-workspace-down / up | `Meta+Ctrl+Down` / `Meta+Ctrl+Up` (KWin stock for global switch — deliberately taken over when `rebindKWinDesktopShortcuts` is on; unset otherwise) |
| focus-workspace 1..9 | unset by default (dynamic `persistent=false` binds, quick-layout-slot style) |
| focus named workspace *name* | unset; per-entry dynamic row |
| move-window-to-workspace down/up | unbound by default (stock KWin binds `Meta+Ctrl+Shift+Arrow` to "Window One Desktop", which we do not rebind) |
| move-window-to-workspace 1..9 / named | unset; dynamic |
| move-column-to-workspace down/up | `Meta+Ctrl+Alt+PgDown` / `PgUp` (the arrow tier is the swap-window quad; scrolling screens only; no-op elsewhere with OSD hint) |
| move-workspace-up/down (reorder) | `Meta+Ctrl+Shift+PgUp` / `PgDn` |
| move-workspace-to-monitor left/right | unbound by default (same `Meta+Ctrl+Shift+Arrow` collision with stock KWin "Window One Desktop") |

Stock-shortcut handling (Phase 5): with the feature + rebind toggle on, the daemon
re-binds KWin's "Switch One Desktop Down/Up/Left/Right" and "Walk Through Desktops"
actions to no-op/our actions via the existing `PhosphorShortcuts::IBackend` KGlobalAccel
path (never KGlobalAccel directly), restoring them on disable. Rationale for rebind over
snap-back-exemption: stock shortcuts iterate the global pool and would trip owner-wins on
nearly every press — exempting them would make foreign desktops routinely visible, which
gutting the model; rebinding is the §1-preferred option. If a binding cannot be captured
(portal backend limits), fall back to snap-back-with-hint and document it.

## 8. Overview-readiness statement

The later overview plan consumes exactly three things this plan ships: (1) the
`workspaceMapChanged` stream + `workspaceMap()` replay with `generation` ordering — the
effect already caches it from Phase 1; (2) the verb set, all invocable daemon-side (the
overview will call them via the existing effect→daemon fireAndForget direction); (3)
per-desktop identity by UUID with live int for `effects->desktops()` addressing. Nothing
here assumes no future renderer: the wire format is additive-versioned, the effect-side
cache is consumer-agnostic, no gesture surface is claimed, and the OSD hints are daemon
surfaces the overview can suppress later via a setting without protocol change.

## 9. Test strategy

- **Unit (LGPL, phosphor-workspaces tests):** WorkspaceMap invariants, position
  arithmetic, serialization round-trip; WorkspaceReconciler driven by scripted
  notification sequences (no D-Bus — the reconciler's inputs are plain calls):
  create-on-occupy, destroy-on-empty incl. the adopt-if-lost race (inject a population
  event between check and echo), trailing-empty repair, first-run adoption ties, external
  creation/count changes, cap degradation, echo-ledger matching + expiry, snap-back
  single-correction guarantee (loop injection test), hotplug migrate/return, restore
  reconcile (candidate vs reality matrices).
- **Existing suites extended:** `test_virtual_desktop_per_screen.cpp` (gate replacement —
  the probe-driven gate, keeping the inference tests for their current callers);
  engine tests for `reapDesktopState`/`renumberDesktopState` across all three engines
  (mutation-style: verify a stale-int key is actually impossible after a renumber pass,
  per the mutation-harness practice). Daemon tests mock KWin D-Bus per existing patterns;
  ctest runs under the dbus-run-session isolation already in place.
- **Live-only (nested kwin_wayland harness, `--virtual` + build-tree QT_PLUGIN_PATH per
  repo memory):** actual KWin `createDesktop` position base + cap constant, per-output
  probe correctness, kwinrc write + `reconfigure()` taking effect without session restart,
  Pager interaction/snap-back feel, stock-shortcut rebind capture, hotplug with real
  output add/remove, effect-unloaded verb timeout behavior.

## 10. Risk register

| Risk | Mitigation |
|---|---|
| int/UUID gap (§5.2): any missed int-keyed map corrupts on renumber | Single shared re-key pass; Phase 1 includes a grep-audit of every `PlacementStateKey` construction site and every `desktop` int stored outside it (sticky pins found already); mutation tests |
| Mode-detection gate (§5.8) | Layered gate (§7); inference demoted, never the gate |
| Renumbering window races | Ledger serialization, verb deferral, clamp interim, settled-reply re-key |
| Echo/feedback loops (Pager/KWin re-assert vs snap-back) | Pending-op ledger + one-correction-per-event queueing; loop unit test |
| External count changes / desktop cap | Follow-and-repair policy + suspension degradation (§4.3) |
| Five-connection `desktopChanged` ordering in `lifecycle_wiring.cpp` | This plan adds NO new connection there (the effect map consumer subscribes to the daemon stream, not to desktopChanged); rationale comments untouched |
| Daemon-restart vs effect bringup ordering for restore | Candidate-map reconcile gated on bringup re-sync (§4.6); replay query covers the reverse order |
| KWin D-Bus has no desktop reorder verb (found in verification) | Fork-5 contiguity weakened to "insert-correct + opportunistic repair" (§4.3 adoption step 3) |
| No `createDesktop`/`removeDesktop` wrappers exist yet (drift) | Added in Phase 1 with the ledger designed around their async echoes from day one |
| fireAndForget verbs are unacknowledged; effect may be unloaded | Population-delta watchdog + timeout drop (§4.2) |
| Sticky-pin divergence (scroll/tiling) interacting with re-key | Pins re-keyed in the same pass; the `ScreenContextTracker.h:180-200` contract read and preserved (pin cleared only with rationale path) |
| Ceiling pressure: reconciler and controller are the two big new files | Pre-planned splits (ledger file; `_verbs.cpp`) |

## 11. Open questions — RESOLVED (2026-08-26)

1. **`PerOutputVirtualDesktops` applies live on `reconfigure()`; no session restart.**
   Verified in KWin 6.7.4 source: `Options::updateSettings()` → `syncFromKcfgc()` →
   `setPerOutputVirtualDesktops(m_settings->perOutputVirtualDesktops())`
   (`options.cpp:732`), with `workspace.cpp:216` connecting
   `Options::perOutputVirtualDesktopsChanged` live to
   `VirtualDesktopManager::setPerOutputVirtualDesktops`. The consent flow says the change
   takes effect immediately. Bonus contract for the disable path: turning the setting OFF
   collapses every output to the active output's current desktop
   (`virtualdesktops.cpp:990-997` — `setCurrent(currentDesktop(activeOutput()), output)`
   for all outputs), so feature-disable lands in a well-defined single-desktop state
   rather than an undefined one.
2. **Default bindings: user decision (2026-08-26) — take over `Meta+Ctrl+Up/Down`** for
   focus-workspace-up/down when stock rebinding is on, as the §7 table proposes. Ship the
   table as written.
