<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# One-shot planning prompt — Dynamic per-monitor workspaces (v3.5)

> This file is a **prompt** to hand to a fresh Claude Code session. Its output is a **plan
> document** (not code): the full implementation plan for dynamic per-monitor workspaces. It
> is self-contained: it carries the behavioral spec, the pre-resolved design forks, the
> verified in-repo seams (inventoried 2026-08-26 — re-verify each before trusting it, this
> repo moves fast), the scope fences, and the required shape of the plan. Paste everything
> below the line as the task. The precedent for this document style is the retired
> `docs/scroll-mode-implementation-prompt.md` (git `49be1881c`), which one-shotted the
> scrolling engine. A niri-style overview is planned SEPARATELY later; this prompt covers
> only the workspace model, but the plan must not paint the overview into a corner (the
> ownership map and its daemon→effect stream are the substrate the overview will render).

---

## PROMPT STARTS HERE

You are planning a major feature for **PlasmaZones** (KDE Plasma window management daemon +
KWin effect; Qt6/KF6, C++20, QML/Kirigami, Wayland-only). Produce a complete implementation
plan — phased, file-level, with test strategy — for **dynamic per-monitor workspaces**:
niri-style per-monitor virtual-desktop lists layered over KWin's shared desktop pool, with
create-on-use, destroy-on-empty, one trailing empty workspace per monitor, and
named/persistent workspaces.

Your deliverable is the plan document. Do NOT write implementation code. Before relying on
any seam named in §5, **read the current source and confirm it still matches**; treat drift
as reality and adapt the plan. Follow `CLAUDE.md` (the plan must respect licensing splits,
file size ceilings, settings architecture, no-ad-hoc-migration policy, plain-prose rules for
any user-facing strings it specifies).

### 1. Invariants that define the feature

These are settled. The plan builds on them; it does not re-litigate them.

- **Mode-agnostic, global layer.** Workspaces sit BELOW the three placement modes
  (Snapping / Tiling / Scrolling). The ownership map and lifecycle logic live in
  phosphor-workspaces + the daemon and apply identically regardless of mode; each
  (screen, desktop, activity) still resolves its own mode as today. Per-mode work is
  strictly reactive plumbing (state reaping/re-keying in each engine, §5.3) — no verb,
  rule, or lifecycle behavior is specific to one mode.
- **KWin's desktop pool stays the single source of truth for desktop existence.** We layer a
  per-monitor **ownership map** (`screenId → ordered list of KWin desktop ids`) on top. Every
  KWin desktop is owned by exactly one screen. A monitor's "workspace list" is its slice.
- **Track desktops by KWin's id strings (UUIDs), never by 1-based index.** KWin renumbers on
  removal; ids are stable. (Today every downstream consumer keys on the int from
  `x11DesktopNumber()` — the plan must address this plumbing gap, see §5.2/§5.8.)
- **Always exactly one trailing empty workspace per monitor.** Occupying it appends a new
  one; emptying a non-named workspace destroys it and the slice closes up. The count tracks
  use.
- **Named workspaces are the stability escape hatch** (see §2). They persist while empty and
  are exempt from destroy-on-empty.
- **Owner screen wins.** If anything outside our control (Pager click, KWin shortcut, window
  rule) puts a window on a desktop owned by another screen, the window follows the desktop's
  owner screen. If an external actor switches a screen to a foreign desktop, we snap back
  and show an OSD hint. Because KWin's stock walk-through-desktops shortcuts iterate the
  GLOBAL pool and would hit foreign desktops constantly, the plan must handle them
  explicitly: replace/rebind them with our per-slice navigation while the feature is on
  (preferred), or exempt them from snap-back with a stated rationale. Snap-back itself must
  be echo-safe (§6.3).
- **The whole feature is opt-in behind a setting** (dynamic workspaces off = current
  behavior untouched). It requires KWin's per-output virtual desktops mode; the plan must
  specify a reliable gate (see the `perScreenModeActive()` hazard in §5.8) — including
  evaluating the option of us writing `PerOutputVirtualDesktops=true` to KWin's config
  (with explicit user consent in the settings UI) when the feature is enabled, since that
  is likely the honest UX rather than telling the user to go set it elsewhere.
- **The Plasma Pager renders the union pool and is unsupported-by-policy** alongside this
  feature (documented). We do not attempt to make it slice-aware.

### 2. Behavioral spec (niri model, distilled)

- **Per-monitor lists.** Each output owns an independent ordered workspace list. No global
  count. Monitor A can have 5 while B has 2.
- **Vertical navigation.** Focus-workspace-up/down walks the monitor's own slice only,
  skipping desktops owned by other screens. Focus-workspace-by-index is positional within
  the slice ("workspace 2 on this monitor" = second in the slice right now).
- **Trailing empty rule** as in §1. Middle workspaces evaporate when emptied (unless named);
  everything below shifts up.
- **Named workspaces**: declared in config; created at daemon startup; persistent while
  empty; stable identity for shortcuts ("focus workspace *chat*"), and pinnable to a
  specific output. They live in the same per-monitor list as dynamic ones, ordered among
  them. Window rules targeting a named workspace are desirable but may be phased later —
  the plan decides.
- **Verbs** (all keyboard; each needs a shortcut entry, see §5.6): focus-workspace-up/down,
  focus-workspace N (indexed, per-monitor), focus named workspace, move-window-to-workspace
  (up/down/N/named), move-column-to-workspace (the scrolling engine's existing column-move
  machinery re-targeted; snapping/tiling windows use the plain window verb), and
  move-workspace-up/down (reorder within the slice), move-workspace-to-monitor
  (left/right/named output).
- **Hotplug: workspaces migrate, not windows.** On output removal, its slice (windows
  intact) is reassigned to a surviving screen, remembering the home output; on replug the
  slice migrates back. The ownership map persists across daemon restarts (state file, not
  config — config is for named-workspace declarations).
- **Engine interaction**: each engine already keys per-(screen, desktop, activity) state.
  New dynamic desktops must get correctly-keyed fresh state; destroyed desktops must have
  their state reaped in ALL THREE engines; renumbering and slice migration must not strand
  or corrupt keys (see §5.3).

### 3. Scope fences (v3.5, this plan)

**Out of scope — do not plan these:**
- **The overview.** It is a separate later plan. This plan only ensures the ownership map,
  its daemon→effect stream, and the verbs are overview-ready (the overview will consume the
  stream and call the same verbs; nothing here may assume "no future renderer").
- Touchpad/touch gestures. Keyboard only. The effect currently registers no gestures; keep
  it that way.
- Per-monitor *rows/grids* — the slice is a flat vertical list.
- Activities integration beyond not breaking the existing (screen, desktop, activity)
  keying.
- X11/XWayland-specific desktop semantics beyond what already works.
- Making the Pager/stock Overview slice-aware.

**Explicitly in scope:** named workspaces (full: config, persistence-while-empty, shortcut
targets, output pinning); hotplug migration; state persistence across restarts; the
daemon→effect ownership-map stream; handling of KWin's stock desktop-switch shortcuts.

### 4. Pre-resolved design forks (do not re-open; refine only)

1. **Shared pool + ownership map** in `libs/phosphor-workspaces` (a new class beside
   `VirtualDesktopManager`, not a new library), daemon is the sole writer. Rejected
   alternative: private per-engine desktop lists invisible to KWin.
2. **Reconciliation = owner screen wins** (§1). Rejected: honoring foreign switches (makes
   our lists advisory) and re-owning desktops on window arrival (churns the map).
3. **First-run adoption**: on enabling the feature, existing desktops are all adopted by the
   screen currently showing them (ties → primary/leftmost), then the trailing-empty rule
   adds what's missing. Desktops created externally later (Pager, KWin config) are adopted
   by the currently focused screen.
4. **Daemon↔effect transport = D-Bus on the existing org.plasmazones interfaces**, following
   the `scrollTabStripsChanged` push-signal + replay-query precedent (§5.5). NOT
   phosphor-ipc (that channel targets phosphor-shell, not the effect).
5. **Slices stay contiguous in KWin's global desktop order** (screen slices concatenated in
   a stable screen order). `createDesktop(position, …)` inserts at the slice-correct global
   position. Rationale: bounds the renumbering blast radius per operation and keeps the
   union Pager at least monotone per screen. The plan specifies the position arithmetic and
   the re-sort on adoption/migration.
6. **Sequencing: model first, verbs second, named workspaces third, hotplug + persistence
   fourth.** Each phase independently buildable and testable with plain shortcuts.

### 5. Verified in-repo seams (inventoried 2026-08-26 — re-verify each)

#### 5.1 phosphor-workspaces (the home for the model)
`libs/phosphor-workspaces/include/PhosphorWorkspaces/VirtualDesktopManager.h` + `src/…cpp`.
Implements `PhosphorEngine::IVirtualDesktopManager`. Talks to KWin via D-Bus service
`org.kde.KWin`, path `/VirtualDesktopManager`, interface
`org.kde.KWin.VirtualDesktopManager`; subscribes to created/removed/current/rows/count
signals; reads the `desktops` property via `org.freedesktop.DBus.Properties.Get` →
`applyDesktopListReply()`. Holds `m_desktopIds` (KWin UUID strings) and
`QHash<QString,int> m_screenDesktops` (screenId → 1-based current), fed by
`updateScreenDesktop()` / cleaned by `removeScreenDesktop()`.
**Nothing in the repo calls `createDesktop(position, name)` / `removeDesktop(id)` /
`setDesktopName(id, name)` yet — this feature is the first caller.**

#### 5.2 Per-screen desktop report path (exact hops; the model for new flows)
1. Effect: `kwin-effect/plasmazoneseffect/lifecycle_wiring.cpp:407` connects the 4-arg
   `EffectsHandler::desktopChanged(old, new, EffectWindow*, LogicalOutput*)`; null output =
   global switch, fanned out to all screens. NOTE: `desktopChanged` has FIVE order-sensitive
   connections in that file (client-area 378, decorations 395, daemon report 407, strip-view
   drop 438, shader transition 499) — read the rationale comments before touching.
2. `PlasmaZonesEffect::reportScreenDesktop()` (`plasmazoneseffect/screens.cpp:105`) dedups,
   then `PhosphorProtocol::ClientHelpers::fireAndForget(…, Interface::WindowTracking,
   "screenDesktopChanged", {screenId, desktop})`.
3. Bringup re-sync bypassing dedup: `plasmazoneseffect/daemon_bringup.cpp:255-270`.
4. Daemon: `WindowTrackingAdaptor::screenDesktopChanged`
   (`src/dbus/windowtrackingadaptor/lifecycle.cpp:906`) →
   `VirtualDesktopManager::updateScreenDesktop()`.
5. Fan-out: `src/daemon/daemon/start.cpp:343-410+`, the `[SEQ A]–[SEQ E]` lambda: cancel
   drag-insert previews, `updateStickyScreenPins`, `setCurrentDesktopForScreen` on all three
   engines, `updateEngineScreens()`, overlay refresh.

**Plumbing gap the plan must solve:** everything downstream keys desktops by 1-based int
(`x11DesktopNumber()`); UUIDs stop at `m_desktopIds`. Dynamic create/destroy renumbers ints.
Decide how far UUID keying must propagate (at minimum: the ownership map, persistence, and
any wire format) versus where a live int→id translation at the chokepoints suffices.
`Daemon::currentDesktopForScreen` (`src/daemon/daemon/osd.cpp:1254`) is the single accessor
chokepoint; the injected provider at
`src/daemon/controllers/contextresolverwiring.cpp:42` is the pattern to extend. Also read
`ScreenContextTracker.h:192` (documented edge case in the `screenDesktopChanged` fan-out).

#### 5.3 Engine state per desktop (all three modes)
- Scrolling has the most: `libs/phosphor-scroll-engine/…/ScrollEngine.h` — one `ScrollState`
  per (screen, desktop, activity), keyed by `PhosphorEngine::PlacementStateKey`
  (`"screenId|desktop|activity"` — an INT desktop inside the key). Desktop switch is a pure
  context swap (`setCurrentDesktopForScreen`), no migration. Sticky-pin divergence
  documented at ~1454-1470 (`currentKeyForScreen` answers with the PINNED desktop while the
  daemon reports the live one).
- Snapping resolves per-(screen, desktop, activity) too (per-screen snap architecture;
  `SnapEngine.cpp`, resnap paths in `libs/phosphor-placement/src/resnap.cpp`), and tiling
  keys per-context state similarly.
**The plan must cover, for ALL THREE engines:** state reaping when a desktop is destroyed,
key stability when KWin renumbers ints (every `PlacementStateKey` containing a stale int is
a corruption vector), and hotplug slice-migration re-keying. Enumerate every per-desktop
map before deciding the mechanism (a rename/renumber pass vs UUID-keyed keys).

#### 5.4 Moving windows between desktops (the verb mechanism)
KWin's D-Bus VirtualDesktopManager has NO window-move method. The only API is effect-side:
`effects->windowToDesktops(w, {desktops})`. So `move-window-to-workspace` (and the
owner-wins window relocation in §6.3) is a daemon→effect COMMAND: the daemon resolves the
target desktop and asks the effect to execute. Verify what the effect already executes on
the daemon's behalf (search the adaptor signals the effect subscribes to in
`kwin-effect/*/wiring.cpp`, e.g. the CompositorBridge interface) and extend that pattern;
the plan must specify the exact signal/method, its payload, and the failure mode when the
effect is not loaded. Moving a window to a desktop owned by another screen additionally
requires placing it on that screen's output — identify the existing cross-screen window
move path (the scrolling monitor-crossing verbs already do this) and reuse it.

#### 5.5 Daemon↔effect transport
D-Bus service `org.plasmazones`, path `/PlasmaZones`, interfaces enumerated in
`libs/phosphor-protocol/include/PhosphorProtocol/ServiceConstants.h` (WindowTracking,
Tiling, Scrolling, Screen, CompositorBridge, …). Daemon→effect = adaptor signal the effect
subscribes to; effect→daemon = `ClientHelpers::fireAndForget`. **Precedent to copy for the
ownership-map stream**: `TilingAdaptor::scrollTabStripsChanged(screenId, stripsJson)`
(`src/dbus/tilingadaptor/tilingadaptor.h:427`) with a replay query (`scrollTabStrips`) for
late-loading effects, subscribed in `kwin-effect/tilinghandler/wiring.cpp:75,127`. Ship the
map as a JSON QString payload like `stripsJson`, not a new marshalled type. Change-gate the
signal (emit only on actual map change).

#### 5.6 Shortcuts
`src/daemon/controllers/shortcutmanager.cpp` — static table `kStaticEntries[]` (line ~49).
Adding one shortcut = 4 edits: table entry (id, `&ConfigDefaults::xShortcut`,
`&Settings::xShortcut`, `QT_TRANSLATE_NOOP` description, lambda emitting a signal), default
in the matching `configdefaults_*` header, `Settings` accessor, wiring in
`src/daemon/daemon/shortcuts_wiring.cpp`. For dynamically-numbered per-workspace and
per-named-workspace shortcuts mirror the indexed quick-layout slots
(`QuickLayoutSlotCount = 9`, `bind(..., persistent=false)` at
`shortcutmanager.cpp:894,1020`). Proposed default bindings must be collision-checked
against `kStaticEntries` AND against KWin's stock desktop-switch bindings, which the plan
must also decide how to neutralize/rebind while the feature is on (§1 owner-wins bullet).

#### 5.7 Settings plumbing for a "Workspaces" group
Add `src/config/configdefaults_workspaces.h` and chain into the `ConfigDefaults`
inheritance (`configdefaults.h`); keys in `configkeys.h` pattern; schema in
`settingsschema*.cpp`; storage arm in a new `src/config/settings/workspaces.cpp` +
load/save/reset in `loadsave.cpp`; settings page registered in
`src/settings/controller/settingscontroller_pageregistration.cpp` (nearest analogue: the
`regVirtual("virtualscreens", …)` entry). New group with defaults needs NO migration
(latest is `configmigration_v6.cpp`). Named-workspace declarations are config; the dynamic
ownership map is runtime state — pick a state-file location and format, NOT config.json.
**Name collision hazard:** a settings page id `"overview"` already exists (monitor-status
dashboard, `settingscontroller_pageregistration.cpp:87`).

The settings UI for this feature is more than keys — plan these surfaces:
- **A "Workspaces" page** (likely AdvancedOnly, like `virtualscreens`): the enable toggle
  with the KWin per-output consent flow (§5.8), the snap-back OSD hint toggle, and whatever
  behavior knobs the plan surfaces. Decide `PageController` vs QML-only `regVirtual` by
  studying both patterns in `src/settings/pages/` first.
- **A named-workspaces list editor** on that page: add/remove/rename entries, reorder, and
  pin-to-output per entry (output picker consistent with existing per-screen UI). This is a
  model + controller + delegate spec, not a sketch. It must integrate with the per-page
  reset/discard machinery every settings page participates in (baseline snapshot +
  `pageOwnedConfigKeys` manifest — find the existing implementation and follow it), and
  edits apply live to the daemon via the normal settings signals.
- **Shortcuts page**: follow the existing per-mode shortcut-page precedent — each mode has
  one (`snapping-shortcuts` / `tiling-shortcuts` / `scrolling-shortcuts` registered via
  `regVirtual` in `settingscontroller_pageregistration.cpp:252,312,378`, QML under
  `src/settings/qml/pages/<mode>/…QuickShortcutsPage.qml`), and
  `src/settings/qml/pages/layouts/QuickLayoutSlotsCard.qml` + the shared
  `components/ShortcutCaptureField.qml` are the indexed-slot capture UI. Give workspaces
  the same shape: a Workspaces shortcuts page (or card on the Workspaces page) listing the
  static verbs, plus per-named-workspace rows derived from the named-workspace list with
  bind/rebind on declaration change (the transient `persistent=false` seam) in the style of
  the quick-layout slot cards.

#### 5.8 KWin per-output mode detection hazard
`perScreenModeActive()` is INFERRED: true iff ≥2 screens currently record diverging
desktops (`tests/unit/core/screens/test_virtual_desktop_per_screen.cpp:90-105`). With
per-output desktops enabled but all monitors on the same desktop it reports false. Nothing
reads or writes KWin's `PerOutputVirtualDesktops` config key. The plan must give dynamic
workspaces a reliable gate — options to evaluate: read KWin's config (kwinrc) directly,
have the effect probe and report the authoritative mode, and/or write the KWin setting
ourselves on feature enable with user consent (§1). The inference must not remain the gate.

### 6. What the plan document must contain

1. **Phases** honoring fork 6 (model + stream → verbs/shortcuts → named workspaces →
   hotplug + persistence → stock-shortcut handling + polish), each phase independently
   buildable and testable, with explicit cut lines.
2. **A file-level change list per phase** (new files with their license per the CLAUDE.md
   split — phosphor-* = LGPL, effect/daemon = GPL; modified files by path), respecting the
   1000-line ceiling (plan splits up front: the ownership map is its own class beside
   `VirtualDesktopManager`), with every new file justified against the §7 reuse mandate.
3. **The ownership-map data model and reconciliation state machine**: exact structure, JSON
   wire format for the daemon→effect stream and the state file, id-vs-index policy per
   layer (§5.2 gap), and owner-wins with its trigger points. The state machine MUST be
   echo-safe: our own `setCurrent`/`createDesktop`/`removeDesktop` calls come back as
   `currentChanged`/`desktopCreated`/`desktopRemoved` and as fresh effect
   `screenDesktopChanged` reports — specify the suppression/correlation mechanism
   (generation counters, pending-op ledger, or equivalent) so snap-back cannot loop against
   the Pager or KWin re-asserting.
4. **Desktop lifecycle algorithms**, each as a precise sequence against the KWin D-Bus
   verbs: create-on-occupy, destroy-on-empty (+ named exemption, AND the race where a
   window maps onto the "empty" desktop between our check and `removeDesktop` — verify
   emptiness at the last moment and adopt-if-lost), trailing-empty maintenance, first-run
   adoption, external-creation adoption, **external count changes** (the System Settings
   desktop-count spinner bulk-adding/removing under us — define the collision policy), the
   **KWin maximum desktop count** (verify the current cap; N monitors × slices can hit it —
   define the degradation), hotplug migration, and the renumbering window between
   `removeDesktop` and the next refresh (`clampScreenDesktopsToCount` interim).
5. **Engine impact, all three modes** (§5.3): the enumerated per-desktop state maps, the
   reap/re-key mechanism, and a statement per mode (the cross-cutting rule: every feature
   needs an arm per mode or a stated reason why not).
6. **Settings & shortcuts inventory**: every new config key with default, every shortcut
   with proposed default binding (collision-checked per §5.6), the named-workspace config
   schema (name, pinned output, order), the KWin stock-shortcut handling, and the full
   settings-UI spec per §5.7 (Workspaces page, named-workspaces list editor with per-page
   reset/discard integration, dynamic shortcut rows, and the consent flow if we write
   KWin's per-output setting).
7. **Overview-readiness statement**: what the later overview plan will consume (the map
   stream, the verbs, the replay query) and confirmation nothing in this plan blocks it.
8. **Test strategy**: unit tests for the ownership map and lifecycle algorithms (mock the
   KWin D-Bus per existing daemon-test patterns), the per-screen tests to extend
   (`test_virtual_desktop_per_screen.cpp`), and what can only be verified live (nested
   kwin_wayland harness exists — see repo memory/docs).
9. **Risk register**: the §5.2 int/UUID gap, §5.8 mode detection, renumbering races,
   echo/feedback loops (§6.3), Pager-driven foreign switches, external count changes and
   the desktop cap, the five-connection `desktopChanged` ordering, daemon-restart vs effect
   bringup-replay ordering for state-file restore, and anything new you find while
   re-verifying seams.
10. **Open questions** ONLY where genuinely unresolvable without the user (target: zero to
    three). Everything in §1–§4 is settled.

### 7. DRY / reuse mandate

Before the plan proposes ANY new class, QML component, wire message, or helper, it must
name the existing thing it reuses or extends — or state why none fits. This repo already
has a component for most of what this feature needs; duplicating one is a plan defect.
Non-exhaustive reuse checklist the plan must walk:
- Shortcut capture/binding UI: `components/ShortcutCaptureField.qml`, the per-mode
  QuickShortcutsPage pattern, `QuickLayoutSlotsCard.qml` (§5.7) — no new capture widget.
- Shortcut registration: `ShortcutManager` static table + transient `persistent=false`
  binds — no parallel registration path.
- Daemon→effect streams: the `scrollTabStripsChanged` signal + replay-query pattern (§5.5)
  — same shape, same JSON-QString payload style, no new marshalled types.
- Per-screen desktop context: extend the existing `IVirtualDesktopManager` /
  `currentDesktopForScreen` chokepoint and the injected-provider pattern (§5.2) — no second
  desktop-state authority anywhere.
- Cross-screen window moves: the existing monitor-crossing move path (§5.4) — no new
  cross-screen placement code.
- Settings pages: existing page registration, per-page reset/discard machinery, list-editor
  and card patterns from `src/settings/qml/pages/` — survey for an existing editable-list
  card before writing a new delegate.
- OSD hints: the daemon's existing OSD service — no new surface.
- State-file persistence: check how existing runtime state (not config) is persisted and
  follow it before inventing a format or location.
Engine reap/re-key logic (§5.3) should live once — in shared code (phosphor-engine or the
daemon fan-out), parameterized per engine — not copy-pasted three times; the plan must
place it explicitly.

### 8. Repo ground rules that bind the plan

- `CLAUDE.md` in full: naming, Qt6 string-literal rules, SPDX/licensing split (LGPL libs vs
  GPL app/effect), settings architecture (`ConfigDefaults` accessors, no ad-hoc migration,
  sparse persistence), i18n (`PhosphorI18n::tr()` in C++), plain-prose user-facing text
  (OSD hints included), file-size ceiling, "solve the root cause, no for-now hacks".
- Desktop identity by stable id, never index, in anything new (the existing int keying is
  the legacy to manage, not the pattern to extend).
- Signals only on actual change; the ownership-map stream must be change-gated.
- Build/test commands and the `BUILD_TESTING=ON` + glslang gotchas are in `CLAUDE.md`.
