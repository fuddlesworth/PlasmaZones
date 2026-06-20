<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Per-Screen Virtual Desktops — Implementation Plan

Tracking: GitHub discussion **#648** — "Desktop Switch OSD fires on cursor movement between displays (Plasma 6.7 per-display virtual desktops)."

Branch: `feat/per-screen-virtual-desktops` (off `v3.1`).

> This plan was produced from a 5-agent codebase + KWin-API investigation. Every
> claim below is grounded in a `file:line` reference or a cited KWin source.

---

## 1. Problem & confirmed root cause

Plasma 6.7 added **"switch desktops independently for each screen"** (per-output
virtual desktops; `kwinrc` `[Windows]/PerOutputVirtualDesktops`, `Bool`, default
`false`). When ON, each monitor has its own current virtual desktop, and KWin
reports the **active monitor's** desktop as the **global**
`org.kde.KWin.VirtualDesktopManager.current`.

PlasmaZones assumes a single global current desktop. Its daemon `VirtualDesktopManager`
subscribes only to KWin's **global** `currentChanged` D-Bus signal
(`libs/phosphor-workspaces/src/VirtualDesktopManager.cpp:44-47`, `:196-207`). So as
the cursor crosses monitors on different desktops, the global value flips and the
daemon's handler (`src/daemon/daemon/start.cpp:197-240`) treats every flip as a real
switch — re-applying layouts, **switching the autotile context**, and firing the
desktop-switch OSD on every screen.

**Confirmed by the reporter's support report** (`journal.log`): a 3-monitor setup with
monitors pinned to desktops 1 / 4 / 7 produced **55 "Virtual desktop changed" events**
in one short session, bouncing `1↔4↔7` with repeating `"Switching autotile context"`
+ layout-OSD churn — purely from cursor movement. The OSD is the visible symptom; the
**per-desktop context thrash is the deeper defect**.

---

## 2. The KWin 6.7 mechanism (verified) — why Path A is deterministic

KWin's **effects API** (the API available to our in-process KWin effect) exposes
per-output desktops in 6.7 (verified against KWin source — see §11 citations):

- `EffectsHandler::currentDesktop(KWin::LogicalOutput* output = nullptr)` — that
  output's current `VirtualDesktop*`; `->x11DesktopNumber()` is the 1-based number
  the daemon already speaks.
- `EffectsHandler::desktopChanged(VirtualDesktop* old, VirtualDesktop* new, EffectWindow* with, KWin::LogicalOutput* output)`
  — the **`output` param was added in 6.7**; it identifies the screen whose desktop
  changed. **It fires only on an actual desktop change for an output — moving the
  cursor between monitors does NOT fire it** (no output's desktop changed).
- `kwinrc [Windows]/PerOutputVirtualDesktops` is readable if explicit mode detection
  is ever needed, but is **not required**: querying `currentDesktop(output)` per
  output is correct under both modes (global mode → all outputs return the same).

KWin's **D-Bus** `org.kde.KWin.VirtualDesktopManager` interface remains **global-only**
(`current` + `currentChanged`) — confirmed. Therefore **per-output desktop data must
come from the effect, not the daemon's VDM D-Bus client.**

**Consequence:** drive all OSD/context off the effect's per-output `desktopChanged`.
Because that signal never fires on cursor movement, the entire cursor-follow detection
problem **dissolves** — no timing/correlation heuristics needed. (The inference
approach, §9 Path B, is retained only as a pre-6.7 fallback.)

---

## 3. Architecture — target data flow

```
                         ┌──────────────────────── KWin (compositor) ───────────────────────┐
                         │  EffectsHandler::desktopChanged(old,new,with, OUTPUT)             │
                         │  EffectsHandler::currentDesktop(output)                           │
                         └───────────────┬───────────────────────────────────────────────────┘
                                         │  (in-process, effect)
        kwin-effect/plasmazoneseffect ── │  on desktopChanged: if output → report ONE screen
                                         │                      if output==null → fan out all
                                         ▼
        D-Bus: org.plasmazones.WindowTracking.screenDesktopChanged(s screenId, i desktop)   [NEW]
                                         │
                                         ▼
        WindowTrackingAdaptor::screenDesktopChanged(screenId, desktop)   [NEW slot]
                                         │  forwards to the desktop authority
                                         ▼
        VirtualDesktopManager::updateScreenDesktop(screenId, desktop)   [NEW]
                                         │  updates per-screen map, emits …
                                         ▼
        VirtualDesktopManager::screenDesktopChanged(screenId, desktop)   [NEW signal]
                                         │
                                         ▼
        Daemon per-screen handler  →  per-screen: autotile ctx + layout + overlay + OSD(screen)
```

The daemon's VDM **keeps** its existing global KWin `currentChanged` subscription, but
**only** to maintain `m_currentDesktop` as the *active/global fallback* for the handful
of genuinely-global callers (§6 bucket C). The global signal **no longer drives OSD or
context** — that moves entirely to the per-screen `screenDesktopChanged` path. This
removes the double-trigger and the cursor-follow thrash at the root.

**One downstream code path.** When per-output mode is OFF, KWin fires
`desktopChanged` with `output == nullptr`; the effect fans that out to one
`screenDesktopChanged` per screen (same desktop). So the daemon only ever wires the
per-screen signal, and mode-off behavior is byte-identical to today (OSD on all
screens, context applied to all).

---

## 4. KWin effect (compositor side)

Files: `kwin-effect/plasmazoneseffect/lifecycle.cpp`, `.../daemon_bringup.cpp`,
`.../screens.cpp`, `.../mouse_drag.cpp`; D-Bus XML `dbus/org.plasmazones.WindowTracking.xml`.

The effect already bridges screen-scoped events to the daemon via
`PhosphorProtocol::ClientHelpers::fireAndForget`. `cursorScreenChanged` is the exact
template (`mouse_drag.cpp:99-115`; XML `:342-347`; screen-id via
`outputScreenId(output)` at `screens.cpp:38-85`).

1. **New D-Bus method** in `dbus/org.plasmazones.WindowTracking.xml` (next to
   `cursorScreenChanged`):
   ```xml
   <method name="screenDesktopChanged">
     <arg name="screenId" type="s" direction="in"/>
     <arg name="desktop"  type="i" direction="in"/>   <!-- 1-based x11DesktopNumber -->
   </method>
   ```
2. **Full-signature `desktopChanged` slot.** Today the effect connects zero-arg lambdas
   to `desktopChanged` (`lifecycle.cpp:487-488`, `:495-497`) which discard `output`.
   Add a slot taking the full 6.7 signature:
   ```cpp
   connect(KWin::effects, &KWin::EffectsHandler::desktopChanged, this,
       [this](KWin::VirtualDesktop*, KWin::VirtualDesktop* nd,
              KWin::EffectWindow*, KWin::LogicalOutput* output) {
           if (!nd || !m_daemonServiceRegistered) return;
           if (output) {
               reportScreenDesktop(outputScreenId(output), nd->x11DesktopNumber());
           } else {                                   // global all-output switch (mode off)
               for (auto* out : KWin::effects->screens())
                   reportScreenDesktop(outputScreenId(out),
                                       KWin::effects->currentDesktop(out)->x11DesktopNumber());
           }
       });
   ```
   `reportScreenDesktop` dedups against a `QHash<QString,int> m_lastScreenDesktop`
   (mirrors `m_lastEffectiveScreenId`) and `fireAndForget`s `screenDesktopChanged`.
   **Use `outputScreenId(output)` (physical), NOT the `vs:`-suffixed effective id** —
   per-output desktops belong to the physical output; virtual-screen subdivisions
   share their parent's desktop.
3. **Bringup re-sync** (`daemon_bringup.cpp:186-213`, next to the cursor re-send):
   iterate `KWin::effects->screens()` and `reportScreenDesktop(...)` for each, so the
   daemon's per-screen map is seeded on every (re)registration and survives daemon
   restarts.

---

## 5. Daemon VirtualDesktopManager — per-screen model

Files: `libs/phosphor-workspaces/{include/PhosphorWorkspaces,src}/VirtualDesktopManager.{h,cpp}`,
`libs/phosphor-engine/include/PhosphorEngine/IVirtualDesktopManager.h`.

### Members
```cpp
int                 m_currentDesktop = 1;   // RETAINED: global/active fallback (legacy)
QHash<QString,int>  m_screenDesktops;       // screenId -> 1-based desktop (per-output, effect-fed)
QStringList         m_knownScreenIds;       // for the mode-off fan-out (daemon-maintained)
```
`m_screenDesktops` is fed by the **effect** (via `updateScreenDesktop`), **not** by the
VDM's KWin D-Bus client (which is global-only).

### API
```cpp
int  currentDesktopForScreen(const QString& screenId) const;  // map else m_currentDesktop
void updateScreenDesktop(const QString& screenId, int desktop); // effect-fed; emits on change
bool perScreenModeActive() const;                              // derived: distinct(map values) > 1
void setKnownScreens(const QStringList& ids);                  // daemon keeps current (screen add/remove)
// RETAINED unchanged: currentDesktop(), setCurrentDesktop(int), desktopCount(), …
```
```cpp
int VirtualDesktopManager::currentDesktopForScreen(const QString& s) const {
    auto it = m_screenDesktops.constFind(s);
    return it != m_screenDesktops.constEnd() ? *it : m_currentDesktop;
}
void VirtualDesktopManager::updateScreenDesktop(const QString& s, int d) {
    if (m_screenDesktops.value(s, -1) == d) return;   // emit-on-change
    m_screenDesktops.insert(s, d);
    Q_EMIT screenDesktopChanged(s, d);
}
```

### Signals
```cpp
void screenDesktopChanged(const QString& screenId, int desktop);  // NEW — primary trigger
void currentDesktopChanged(int desktop);                          // RETAINED (global fallback only)
void desktopCountChanged(int count);                              // RETAINED unchanged
```

### Mode-off fan-out
On a global switch in mode-off, the **effect** already fans out one
`screenDesktopChanged` per screen (output==null path, §4.2). The VDM's global
`onKWinCurrentChanged` (`VirtualDesktopManager.cpp:196-207`) therefore only updates
`m_currentDesktop` + emits the legacy `currentDesktopChanged`; it does **not** fan out
(the effect did). If, for resilience, we ever need the VDM to fan out independently of
the effect, it can loop `m_knownScreenIds` calling `updateScreenDesktop(id, newDesktop)`
— but the effect path is authoritative.

### Interface ripple
Add `currentDesktopForScreen(const QString&)` and `perScreenModeActive()` to
`IVirtualDesktopManager` **as non-pure (defaulted)** methods
(`return currentDesktop();` / `return false;`). Rationale: the **only** interface
consumer is `SnapEngine` (holds `IVirtualDesktopManager*`, calls `currentDesktop()` at 7
sites); **no test subclasses the interface** (tests use the concrete class). Non-pure
defaults keep SnapEngine and every test source-compatible. The `screenDesktopChanged`
**signal lives on the concrete QObject**, not the interface.

### Count-changed (`desktopCountChanged`) — stays global
Desktop add/remove is workspace-wide (`start.cpp:243-282`). One addition: after a
removal, prune/clamp any `m_screenDesktops` entry whose desktop number now exceeds the
count (fold into `onKWinDesktopRemoved`, `VirtualDesktopManager.cpp:215-219`), emitting
`screenDesktopChanged` for any screen whose number shifted.

---

## 6. WindowTrackingAdaptor (receives the effect report)

File: `src/dbus/windowtrackingadaptor.cpp`.

Add the D-Bus slot mirroring `cursorScreenChanged` (`:873`):
```cpp
void WindowTrackingAdaptor::screenDesktopChanged(const QString& screenId, int desktop) {
    if (screenId.isEmpty() || desktop < 1) return;
    if (m_virtualDesktopManager) m_virtualDesktopManager->updateScreenDesktop(screenId, desktop);
}
```
The adaptor already holds the VDM (used for `currentDesktop()` mirror at `:251-253`).

---

## 7. Daemon handler rewrite (the core behavior change)

File: `src/daemon/daemon/start.cpp:197-291`, `src/daemon/daemon/osd.cpp`.

**Move the context/OSD handler off `currentDesktopChanged` and onto
`screenDesktopChanged(screenId, desktop)`.** The handler now operates on **one screen**.

What the current lambda (`start.cpp:197-240`) does, and the new scope:

| # | Action (line) | New scope |
|---|---|---|
| 1 | `overlayService->setCurrentVirtualDesktop` (202) | **per-screen** → `setCurrentVirtualDesktopForScreen(screenId, desktop)` (§8) |
| 2 | `layoutManager->setCurrentVirtualDesktop` (203) | **per-screen** → `setCurrentVirtualDesktopForScreen(screenId, desktop)` (§8) |
| 3 | `unifiedLayoutController->setCurrentVirtualDesktop` (204-206) | global (active/focused screen's desktop) |
| 4 | `cancelDragInsertPreview()` (210-212) | cancel iff `screenId == previewScreen` (else harmless eager cancel) |
| 5 | `updateStickyScreenPins(...)` (217-222) | global, **runs BEFORE any desktop ctx change** [SEQ B] |
| 6 | `autotileEngine->setCurrentDesktop` (226-228) | **per-screen** → `setCurrentDesktopForScreen(screenId, desktop)` (§8), **BEFORE #7/#8** [SEQ C] |
| 7 | `updateAutotileScreens()` (230) | recompute (global; cheap, idempotent) |
| 8 | `syncModeFromAssignments()` (234) | now reads `currentDesktopForScreen(focusedScreen)` |
| 9 | `overlayService->updateGeometries()` (235-237) | global, idempotent |
| 10 | `showDesktopSwitchOsd(desktop, activity)` (239) | **per-screen** → OSD only on `screenId` |

**Sequencing invariants (preserved, see comments at `start.cpp:207-230`):**
[SEQ A] cancel drag-insert preview → [SEQ B] sticky-pin screens → [SEQ C]
`setCurrentDesktopForScreen` → [SEQ D] push layout/overlay/controller → [SEQ E]
`updateAutotileScreens()` then `syncModeFromAssignments()` → OSD. SEQ C must precede
both SEQ E steps (engine resolves `TilingState` by the new per-screen key).

**Per-screen OSD.** Factor the single-screen body out of `showOsdForAllScreens`
(`osd.cpp:490-561`, the loop interior at `:508-559`) into
`showDesktopSwitchOsdForScreen(screenId, desktop, activity)`. Keep `showOsdForAllScreens`
for the genuinely-global callers (startup welcome / activity OSD,
`signals.cpp:839/870/891`). In mode-off the effect fan-out calls the per-screen OSD once
per screen → visually identical to today.

**Initial-desktop wiring** (`start.cpp:284-291`): loop `effectiveScreenIds()`, seeding
each component with `currentDesktopForScreen(screenId)` and the engine via
`setCurrentDesktopForScreen` per screen. Mode-off collapses to today.

---

## 8. Engine + Layout + Overlay per-screen plumbing

### 8a. AutotileEngine — generalize the existing per-screen mechanism
Files: `libs/phosphor-tile-engine/include/PhosphorTileEngine/AutotileEngine.h`,
`libs/phosphor-tile-engine/src/AutotileEngine.cpp`, `.../NavigationController.cpp`.

The engine **already** resolves a per-screen desktop:
`currentKeyForScreen(screenId)` (`AutotileEngine.h:1156-1161`) returns
`m_screenDesktopOverride[screenId]` else `m_currentDesktop`. `m_screenDesktopOverride`
(`:1410`) exists today, written **only** by the `virtualdesktopsonlyonprimary`
sticky-pin lifecycle.

**Two-map design (chosen).** The sticky pin and a per-output-VD value both want a
screen's entry — two writers, one map would clobber. Keep them separate:
- `m_screenDesktopOverride` — **sticky-pin** map (unchanged; 5 existing sites at
  `AutotileEngine.cpp:329-335, 547-557, 774, 1078`).
- `m_screenCurrentDesktop` (**NEW** `QHash<QString,int>`) — per-output-VD map.
- Precedence in `currentKeyForScreen`: **sticky-pin override WINS**, else
  per-output-VD, else `m_currentDesktop`. Rationale: the sticky pin is a *correctness*
  constraint (sticky on-all-desktops windows must keep their state on the desktop where
  they live, `AutotileEngine.h:1150-1155`); per-output-VD is the *normal* input. The
  pin must win or a sticky screen's state orphans — the exact bug the pin prevents.

New API (add to `ITileEngine` next to `setCurrentDesktop`, `AutotileEngine.h:281`):
```cpp
void setCurrentDesktopForScreen(const QString& screenId, int desktop) override; // writes m_screenCurrentDesktop
void clearCurrentDesktopForScreen(const QString& screenId) override;            // revert to fallback
```

**`setCurrentDesktopForScreen` is a PURE context swap — NO state migration.** This is
the central correctness point: per-desktop `TilingState`s must stay put (the old
desktop's state reappears when that screen returns). Only the **sticky-unpin** path
migrates (`AutotileEngine.cpp:561-591`), and only because the pin forced two logical
desktops to share one state. Do not migrate on a per-output-VD change.

**Read sites to convert** to `key.desktop == currentKeyForScreen(key.screenId).desktop`
(reference impl already at `NavigationController.cpp:405, 813`):
`AutotileEngine.cpp:194, 733, 1770, 1832, 3694, 3708, 3751`. The setter
(`:457-485`) and unpin-migration (`:562-589`) keep raw `m_currentDesktop`.

**Switch-arming flag** `m_isDesktopContextSwitch` (`:482`, consumed globally at `:605,
:792`): keep global initially — arming the whole effect pass on any per-screen change
is over-broad but idempotent (catch-scan re-adds, `:607-609`). Promote to a
`QSet<QString>` only if the over-broad pass proves costly.

**Prune:** extend `pruneStatesForDesktop` (`:1055-1089`) to sweep `m_screenCurrentDesktop`
by value exactly as it already sweeps `m_screenDesktopOverride` at `:1078`.

### 8b. LayoutRegistry — one change point
Files: `libs/phosphor-zones/{include/PhosphorZones,src}/LayoutRegistry.*`,
`IZoneLayoutRegistry.h`.

The layout layer is already per-screen at lookup (`layoutForScreen(screenId, vd, act)`,
`modeForScreen`, …). Only `resolveLayoutForScreen` (`LayoutRegistry.h:300-303`) injects
the desktop from the single member `m_currentVirtualDesktop` (`:752`). Change:
- Add `QHash<QString,int> m_screenVirtualDesktop` + `setCurrentVirtualDesktopForScreen`
  + `currentVirtualDesktopForScreen(screenId)` (override else `m_currentVirtualDesktop`).
- `resolveLayoutForScreen(screenId)` → `layoutForScreen(screenId, currentVirtualDesktopForScreen(screenId), m_currentActivity)`.
  **Every caller already passes a `screenId`** (~15 sites), so this single change
  propagates per-screen resolution everywhere with zero caller edits.
- `currentVirtualDesktop()` (`:447`) stays as the global accessor.

### 8c. OverlayService + controllers — per-screen members
Files: `src/daemon/overlayservice.{h,cpp}`, `unifiedlayoutcontroller.cpp`,
`zoneselectorcontroller.cpp`.

`OverlayService::m_currentVirtualDesktop` (`overlayservice.h:639`) is used at 5 sites
that **already have a `screenId` in scope** (`overlayservice.cpp:569, 592, 604, 659,
674, 710`). Make it a `QHash<QString,int>` with `setCurrentVirtualDesktopForScreen`;
each usage resolves per its local `screenId`. This single sink change **auto-fixes ~16
of the bucket-A reads** in §9. `ZoneSelectorController` is already screen-targeted
(`m_screenId`); track that screen's desktop. `UnifiedLayoutController` is focused-screen
context (`m_currentScreenName`); store per-screen or derive at use.

---

## 9. Call-site audit — `currentDesktop()` → `currentDesktopForScreen(screenId)`

Full per-site table lives in the agent report; summary (grep-verified, repo-wide):

- **(A) screenId in scope** (must become per-screen, else reads the wrong screen's
  desktop): ~41 direct sites + ~16 cached-member reads fixed via the §8c sink change
  ≈ **57**. Canonical seam: `ContextResolver::handleFor(screenId)` pulling the global
  desktop (`libs/phosphor-context-resolver/src/contextresolver.cpp:47, 79`). Notable:
  `daemon.cpp:1283`, `autotile.cpp:447`, snap `commit.cpp:44`/`calculate.cpp:168,231`,
  `geometryutils.cpp:105`, `screenmoderouter.cpp:41`, `layoutregistry.cpp:241,261,300,302`.
  `daemongeometryresolver.cpp:19` requires a callback signature change
  (`std::function<int()>` → `std::function<int(QString)>`).
- **(B) fan-out per-screen** (loop, each screen its own desktop): **14**, incl.
  `autotile.cpp:56,229,293,345,476`, `daemon.cpp:1449`, `osd.cpp:416`,
  `start.cpp:129,812,895`. **API change:** `populateResnapBufferForAllScreens(excl,
  incl, desktopFilter)` (`resnap.cpp:28`) applies one filter to all screens
  (`navigation.cpp:422`) — must become per-screen.
- **(C) genuinely global** (keep global / active-screen fallback): **~14** — startup
  wiring, global welcome/activity OSD (`signals.cpp:509,839,870,891`,
  `start.cpp:286,368`), boot-time state restore, the `globalHandle`, and the helper
  *definitions* themselves. `crosssurfaceresolver.cpp:52,58` takes desktop as a param
  (its bucket-A callers pass the per-screen value).
- **(D) ambiguous — design decisions resolved in §10:** 3 sites.

**Sinks made per-screen:** OverlayService, LayoutRegistry, ContextResolver/IWorkspaceState,
UnifiedLayoutController, ZoneSelectorController. **AutotileEngine's `m_currentDesktop`
stays the global fallback** (per-screen via the override maps).

---

## 10. The 3 ambiguous (D) decisions — resolved

1. **`snap/lifecycle.cpp:597` `capturePlacement` — `p.virtualDesktop = currentVirtualDesktop()`.**
   `effScreen` is in scope (`:586`). The persisted float-back record must stamp the
   **window's screen's** desktop, else float-back restores to the wrong desktop.
   → **Bucket A:** `currentDesktopForScreen(effScreen)`.
2. **`snap/float.cpp:393` `handoffReceive` — `setFloatingOnScreen(win, ctx.toScreenId, currentDesktop)`.**
   The stored desktop must be the **destination** screen's desktop. → use
   `ctx.toDesktop` if `HandoffContext` carries it; else `currentDesktopForScreen(ctx.toScreenId)`.
3. **`placement/resnap.cpp:248` `pendingRestoreGeometries` — global desktop filter.**
   Each record is filtered (`:284`) by "is this the current desktop?" — must be answered
   against the **record's own screen** (`p.screenId`, in scope ~`:270`). → filter each
   record against `currentDesktopForScreen(p.screenId)`.

Plus a confirmation flag: `navigation_crosssurface.cpp:73` (cross-mode output to a
neighbour) uses the **neighbour** screen's desktop — confirm intended (the neighbour
may be on a different desktop than the source).

---

## 11. Detection paths

- **Path A (primary, Plasma 6.7+): effect-sourced, deterministic.** §2–§7. The effect
  reports each output's real desktop; cursor moves fire nothing; the daemon ignores the
  global `currentChanged` for OSD/context. **No correlation heuristics.** This is the
  recommended implementation.
- **Path B (fallback, pre-6.7 only): inference.** If `desktopChanged`'s `output`
  param / `currentDesktop(output)` are unavailable, correlate the global `currentChanged`
  with the effect's existing `cursorScreenChanged`: a global flip *with* a recent
  cursor-screen change (within an ε window, order-independent) = a follow (record the
  cursor screen's desktop, suppress OSD/context); *without* = a real switch of the
  cursor's screen. Mode is derived from divergence (two screens, different desktops).
  Path B is **strictly inferior** (unresolvable cross-bus ordering race; can't enumerate
  all screens at startup) and is **out of scope unless pre-6.7 support is required.**

KWin source citations (verified):
`https://invent.kde.org/plasma/kwin/-/raw/Plasma/6.7/src/effect/effecthandler.h`
(`currentDesktop(output)`, `desktopChanged(...,output)`);
`https://invent.kde.org/plasma/kwin/-/raw/master/src/kwin.kcfg`
(`[Windows]/PerOutputVirtualDesktops`);
per-output commit `fa6c4a424a5c8bcefddcb163852aefae8313a9a1`.

---

## 12. Backward compatibility

- **Plasma < 6.7 / setting OFF:** KWin fires `desktopChanged(output=null)`; the effect
  fans out one `screenDesktopChanged` per screen (same desktop). The daemon applies
  context + OSD to every screen — **byte-identical to today**. `m_screenDesktops` never
  diverges; `perScreenModeActive()` stays false; all `currentDesktopForScreen` calls
  fall through to `m_currentDesktop`.
- **Single monitor:** divergence impossible → legacy path exactly.
- **Interface additions are non-pure defaulted** → SnapEngine + all existing tests
  compile unchanged.

---

## 13. Test plan

Harness gaps today: **no `IVirtualDesktopManager` mock**, **no OSD spy**, and the
daemon desktop-switch lambda is **untested**. So most coverage is **new**.

- **(a) Effect report → VDM map (unit).** `VirtualDesktopManager::updateScreenDesktop`
  + `currentDesktopForScreen` + `perScreenModeActive` + emit-on-change + count-removal
  pruning. Concrete `VirtualDesktopManager` (no D-Bus needed; `m_useKWinDBus=false`).
- **(b) #648 regression (the key test).** Simulate: screen2 on desktop 2, cursor crosses
  to screen2 → **no `screenDesktopChanged`** (cursor moves don't change an output's
  desktop) → assert **no desktop-switch OSD** and **no autotile context switch**. Then a
  *real* switch of screen2 → `screenDesktopChanged(screen2, 2)` → OSD on screen2 only +
  engine `setCurrentDesktopForScreen(screen2, 2)`.
- **(c) Per-screen OSD scoping.** A real switch of one screen fires OSD only on that
  screen's surface. Requires the `showDesktopSwitchOsdForScreen` factor-out (§7).
- **(d) Engine two-map precedence.** Sticky-pin override wins over per-output-VD;
  `setCurrentDesktopForScreen` does not migrate state; desktop-removal prunes both maps.
- **(e) Mode-off fan-out.** `desktopChanged(output=null)` → all screens updated, OSD on
  all → matches legacy.

Recommended seam: extract the daemon classification/scoping into a small testable unit
(`PerScreenDesktopTracker`, LGPL, `libs/phosphor-workspaces`) so the core logic tests
with zero D-Bus/KWin/QML. New tests under `tests/unit/` via `p_add_test`; a lib-resident
tracker's test goes under `libs/phosphor-workspaces/tests/` (LGPL header).

---

## 14. Risks

1. **Call-site completeness (highest).** A missed bucket-A site reads the wrong screen's
   desktop in production. Mitigation: the §9 table is the checklist; the §8b/§8c sink
   changes collapse ~16 of them; grep `currentDesktop()` / `m_currentVirtualDesktop` to
   verify zero stragglers post-refactor.
2. **Sticky-pin vs per-output-VD clobber.** Mitigated by the two-map design (§8a) with
   sticky precedence.
3. **State migration.** `setCurrentDesktopForScreen` must be a pure context swap (no
   migration) — only sticky-unpin migrates.
4. **`m_isDesktopContextSwitch` over-broad arming** — accepted (idempotent); revisit if
   costly.
5. **KWin effect API drift.** `desktopChanged`'s `output` param is 6.7-only; older KWin
   uses Path B or shows today's behavior. Guard the effect connection accordingly.

---

## 15. Phasing (each phase builds + tests green)

1. **VDM model** — per-screen map, `currentDesktopForScreen`, `updateScreenDesktop`,
   `screenDesktopChanged` signal, non-pure interface additions, count-removal pruning.
   Tests (a). *No behavior change yet.*
2. **Effect + D-Bus + WTA** — `screenDesktopChanged` XML method, effect full-signature
   `desktopChanged` slot + bringup re-sync, WTA forwarder. *Wires the data source.*
3. **Engine** — two-map design, `setCurrentDesktopForScreen`, read-site conversions,
   prune. Tests (d).
4. **Layout + Overlay sinks** — `resolveLayoutForScreen` per-screen, OverlayService /
   controllers per-screen members. Collapses bucket-A reads.
5. **Daemon handler** — move to `screenDesktopChanged`, per-screen OSD factor-out,
   initial-desktop loop, sequencing. Tests (b), (c), (e).
6. **Call-site sweep** — remaining bucket-A/B sites + the 3 (D) decisions + the
   `populateResnapBufferForAllScreens` API change. Final grep sweep.
7. **Audit + regression on a real Plasma 6.7 session.**
