# Animation Shader Wire-Up — Plan

Branch: `feat/animation-shader-wireup` (off `origin/v3`)

**Status (commit chain):**

- ✅ Phase 0a — `applySnapGeometry` profilePath param (`21604667`)
- ✅ Phase 0b — `shaderProfileTreeChanged` live-reload (kwin-effect already wired; daemon now wired in `eabb6c21`)
- ✅ Phase 0c — `ShaderProfile::parameters` → GLSL uniforms on both render paths (`f07ebd95`)
- ✅ Phase 0d — per-window event conflict documented (`080f825b`)
- ✅ Phase 1 — kwin-effect window-lifecycle wire-up (`080f825b`)
- ✅ Phase 2 — folded into 0a
- ✅ Phase 3a — `panel.popup.{zoneSelector,layoutPicker,snapAssist}` paths added (`eabb6c21`)
- ✅ Phase 3b — daemon overlay shader fields wired (`eabb6c21`)
- ✅ Phase 4a — `zone.flash` → `zone.layoutSwitchIn` rename
- ⏸ Phase 4 (full) — `PhosphorShaderTransition` QML element — deferred to follow-up branch
- ⏸ Phase 5 — workspace events — deferred (per plan, requires new KWin signal connects + per-screen shader pass; out-of-scope)

The infrastructure for shader-driven animation transitions is fully built (`AnimationShaderRegistry`, `ShaderProfileTree`, `SurfaceAnimator::Config` shader fields, kwin-effect's `OffscreenEffect`-redirect path). But it's only consulted in **one** place: `kwin-effect/plasmazoneseffect.cpp:3496` for `zone.snapIn`. Everything else — window lifecycle, daemon overlays, layout switching, focus, drag — runs motion-only.

This plan wires the rest, in dependency order, with each phase shippable independently.

---

## Audit findings (folded in)

Four parallel analyst agents mapped the gaps. Key findings each phase addresses:

1. **`applySnapGeometry` is a chokepoint that lies.** It resolves `zone.snapIn` for *every* motion that flows through it (snap, snap-out, resnap, layout-cycle, autotile rotate, restore, drag-out, ...) — but it has no idea which logical event it's animating. ~7 call sites need to inject the correct `ProfilePath`.
2. **The kwin-effect and daemon use two different shader render paths.** Kwin uses `KWin::ShaderManager::generateCustomShader` + `OffscreenEffect::redirect`, only feeds `iTime`+`iResolution` uniforms. Daemon uses `PhosphorRendering::ShaderEffect` (QQuickItem subclass) with the rich set (`iMouse`, `iTimeDelta`, `iFrame`, `shaderParams`). Same `.frag` source. Authors targeting the rich set silently get garbage on the kwin path.
3. **`ShaderProfile::parameters` is dead on both paths.** Designed-but-unwired all the way through; only `tests/test_shaderprofile.cpp` references `effectiveParameters()`.
4. **Live reload is broken.** `ISettings::shaderProfileTreeChanged` (`settings.cpp:1007`) has **zero consumers**. Editing the tree at runtime requires a process restart. `SurfaceAnimator::Config::showShaderEffectId` is captured at `registerConfigForRole` time and never re-pushed.
5. **Per-window event conflict.** `m_shaderTransitions` is keyed on `EffectWindow*`, not event. Two events overlapping on one window — second `beginShaderTransition` calls `endShaderTransition` first, killing the first transition. The shader timeline is also stolen from `m_windowAnimator`'s single animation slot.
6. **All 4 daemon `build*Config()` functions hardcode shader fields to `{}`** (`src/daemon/overlayservice.cpp:189-250`).
7. **Workspace events don't reach the kwin-effect.** No connections to `EffectsHandler::desktopChanged` / `currentDesktopChanged` / `showingDesktopChanged`. Stock KWin effects own these. Wiring would need new connects + a per-screen shader pass (not per-window OffscreenEffect).
8. **Editor / Settings QML use `widget.*` paths** — deliberate polish profiles, not shader-eligible. Out of scope.
9. **Overlays are already 100% C++-SurfaceAnimator-driven** for show/hide. No QML-side shader element exists; `phosphor-animation-shaders` doesn't link `Qt6::Qml`. A QML `PhosphorShaderTransition` element would only be needed for *in-content* state changes (e.g. `SnapAssistOverlay`'s zone.snapIn Behaviors at lines 161/169, `ZoneOverlay`'s `zone.flash` at line 274). Lower priority.

---

## Phase 0 — Foundation (must land first; unblocks every later phase)

### 0a. Fix `applySnapGeometry` path-misattribution

**Problem.** `applySnapGeometry` hardcodes `m_shaderProfileTree.resolve("zone.snapIn")` (`plasmazoneseffect.cpp:3496`). Every caller — restore, resnap, rotate, snap-out, drag-restore, layout-switch — gets `zone.snapIn`'s shader. This is wrong before we even add more events.

**Fix.** Add `const QString& profilePath` parameter to `applySnapGeometry` (default `ProfilePaths::ZoneSnapIn` for source-compat). Update each caller:

| Call site | File:Line | Pass |
|---|---|---|
| Snap-restore on window add | `:741` | `WindowOpen` (or skip — `skipAnimation=true`) |
| `slotApplyGeometryRequested` (sizeOnly) | `:2537` | `ZoneSnapResize` |
| `slotApplyGeometryRequested` (zoneId empty → float restore) | `:2577` | `ZoneSnapOut` |
| `slotApplyGeometriesBatch` (action="resnap"/"retile") | `:2678` | `ZoneLayoutSwitchIn` |
| `slotApplyGeometriesBatch` (action="rotate") | `:2678` (gated) | `ZoneSnapIn` |
| Snap-on-windowSnapped | `:2489` | `ZoneSnapIn` |
| `slotEndDrag` RestoreSize | `:3270` | `ZoneSnapOut` |
| `slotRestoreSizeDuringDrag` | `:3546` | `ZoneSnapOut` |

Use `PhosphorAnimation::ProfilePaths::*` constants, not string literals.

**Test.** Add a unit test or trace log proving each call site hits a *different* shader effect when the user assigns distinct profiles to each path.

### 0b. Wire `shaderProfileTreeChanged` live-reload

**kwin-effect side.** `loadShaderProfileFromDbus()` is called once at startup (`plasmazoneseffect.cpp:1867`). Add a re-fetch in `slotSettingsChanged()` (`:1556`) so subsequent edits propagate. Already runs on the daemon's `settingsChanged` D-Bus signal — we just need to call `loadShaderProfileFromDbus()` from there.

**Daemon side.** `OverlayService::setupSurfaceAnimator` registers configs once at construction. Two changes needed:
- Move the shader-leg fields out of `build*Config()` lambdas and into a new `applyShaderProfilesToAnimator(ShaderProfileTree)` helper that the constructor calls once and `shaderProfileTreeChanged` re-calls.
- Connect `m_settings → shaderProfileTreeChanged → applyShaderProfilesToAnimator(m_settings->shaderProfileTree())` in `OverlayService::setSettings` next to existing settings-signal hookups; track the connection in a `QMetaObject::Connection` member.

Caveat: `registerConfigForRole` only affects subsequent `Surface::show()/hide()` lookups — surfaces mid-animation keep their bound config. That's the right behavior, matches motion live-reload semantics.

### 0c. Wire `ShaderProfile::parameters` to GLSL uniforms

**kwin-effect path.** `paintWindow` (`:4085`) currently only sets `iTime` and `iResolution`. Extend to iterate `ShaderProfile::effectiveParameters()` and call `GLShader::setUniform` for each. Need to look up uniform locations once per cache entry (alongside `iTimeLoc`/`iResolutionLoc`). Cache `QHash<QString, int>` of param-name → location in `ShaderTransition::cached`.

**Daemon path.** `runLeg` (`surfaceanimator.cpp:400-414`) creates a `PhosphorRendering::ShaderEffect`. After `setShaderSource(...)`, call `shaderItem->setShaderParams(profile.effectiveParameters())`. `setShaderParams` already exists on `ShaderEffect` (used by zone-background shaders).

**Test.** Author a tiny test shader that reads a custom param and renders it; verify both paths drive it.

### 0d. Fix per-event timeline conflict

**Problem.** `m_shaderTransitions[window]` is one slot per window. Two events on the same window clobber. Current behavior is acceptable for `zone.snapIn` because nothing else competes — but with `window.move` + `window.focus` + `zone.snapResize` all in flight, one event eats another.

**Fix options (pick A unless we hit visual gaps in testing):**

- **A. Last-event-wins, document it.** Keep `m_shaderTransitions` per-window. Document that overlapping events on the same window resolve to the most recent. Most events that overlap on one window are short (<300ms) so this is rarely visible.
- **B. Per-event timeline.** Replace single-slot map with `std::unordered_map<EffectWindow*, std::vector<ShaderTransition>>`. Each transition gets its own `AnimatedValue<qreal>` driven independently of `m_windowAnimator`'s geometry timeline. Composites multiple shaders per window in `paintWindow` (multiplicative alpha or sum based on shader contract). Significantly more complex and we'd need to define a composition rule per shader.

Recommend A for now; revisit if testing surfaces visible bugs.

---

## Phase 1 — kwin-effect window lifecycle (Pattern B)

For each event below, insert a standalone resolve+`beginShaderTransition` block at the indicated slot entry. Since these events have no `m_windowAnimator` completion callback, drive `endShaderTransition` from a `QTimer::singleShot(durationMs)` set to the resolved motion profile's duration (or a reasonable default like 300ms when motion isn't involved).

| Path | Slot / connect site | File:Line |
|---|---|---|
| `window.open` | `slotWindowAdded` (after `setupWindowConnections`) | `:690` |
| `window.close` | `slotWindowClosed` (top, before `endShaderTransition`) | `:783` |
| `window.minimize` / `window.unminimize` | `slotWindowMinimizedChanged` (gated on `w->isMinimized()`) | `:2945` |
| `window.maximize` / `window.unmaximize` | new lambda parallel to AutotileHandler's `slotWindowMaximizedStateChanged` connect | `:1029` |
| `window.move` | `dragStarted` lambda body | `:233` |
| `window.resize` | `windowStartUserMovedResized` lambda (gated on `w->isUserResize()`) | `:1021` |
| `window.focus` | `notifyWindowActivated` (after rejection-filter check) | `:3703-3707` |

Helper: factor out a `tryBeginShaderForEvent(EffectWindow*, QStringView profilePath, std::chrono::milliseconds duration)` so the eight call sites stay a single line each.

---

## Phase 2 — kwin-effect zone events (Pattern A — chokepoint, completes 0a)

These are already covered by 0a's path-parameter cleanup. Listed here so we can verify each works end-to-end before Phase 3:

- `zone.snapIn` ✓ (already works; verify)
- `zone.snapOut` (drag-out, restore, sizeOnly+empty)
- `zone.snapResize`
- `zone.layoutSwitchIn` / `zone.layoutSwitchOut` (resnap, retile, rotate)
- `zone.highlight` — **not** in `applySnapGeometry`'s flow. This is rendered inside the overlay (`ZoneItem.qml`'s color/opacity Behaviors). Out of kwin-effect scope; covered in Phase 4.

---

## Phase 3 — Daemon overlay shader fields (completes 0b's daemon side)

### 3a. Add distinct sub-paths under `panel.popup`

To let users tune zone selector, layout picker, and snap-assist independently while keeping a `panel.popup` baseline that covers all three, add three new constants to `libs/phosphor-animation/include/PhosphorAnimation/ProfilePaths.h`:

```cpp
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupZoneSelector;  // "panel.popup.zoneSelector"
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupLayoutPicker;  // "panel.popup.layoutPicker"
PHOSPHORANIMATION_EXPORT extern const QString PanelPopupSnapAssist;    // "panel.popup.snapAssist"
```

Walk-up inheritance (`ShaderProfileTree::resolve`) means a user who only sets `panel.popup` gets all three using the same effect, but anyone wanting the layout picker to differ overrides `panel.popup.layoutPicker` and the others keep the baseline. This pattern matches the existing `widget.*` archetype split in `ProfilePaths.h:137-156`.

### 3b. Per-role wiring

| Role | `build*Config()` | `showPath` | `hidePath` | keepMappedOnHide |
|---|---|---|---|---|
| `Notification` | `buildOsdConfig` | `osd.show` | `osd.hide` | true (warmed) |
| `LayoutPicker` | `buildLayoutPickerConfig` | `panel.popup.layoutPicker` | `panel.popup.layoutPicker` (or `widget.fade`) | true |
| `ZoneSelector` | `buildZoneSelectorConfig` | `panel.popup.zoneSelector` | `panel.popup.zoneSelector` (or `widget.fade`) | true |
| `SnapAssist` | `buildSnapAssistConfig` | `panel.popup.snapAssist` | `{}` (dead — destroy-on-hide) | false |

**Out-of-scope surfaces** (deliberate animator bypasses, documented at `overlayservice.cpp:258-267`):
- `Overlay` (zone overlay) — drag path uses direct `window->hide()` for sub-frame toggling.
- `ShaderPreview` (editor preview) — imperative editor control.

Wiring re-uses the helper from 0b: each builder takes a `const ShaderProfileTree&` (or queries it indirectly), resolves the relevant `ProfilePath`, and fills `showShaderEffectId = profile.effectiveEffectId()` plus `showShaderProfile = profilePath` for the motion-tree time-curve coupling.

---

## Phase 4 — In-content QML state changes (deferred to follow-up branch)

Three QML sites where the *content* (not the surface) animates a logical state change with `PhosphorMotionAnimation` and would benefit from a parallel shader transition:

| File:Line | Profile | What |
|---|---|---|
| `src/ui/SnapAssistOverlay.qml:161,169` | `zone.snapIn` | snap-target color/border |
| `src/ui/ZoneOverlay.qml:279` | `zone.layoutSwitchIn` (✅ renamed from `zone.flash` for taxonomy parity) | full-screen flash on layout cycle |
| `src/ui/ZoneItem.qml:64,71,107,130` | `zone.highlight` | per-zone tile highlight during drag |

**Status: deferred to follow-up branch.** Surface-level shader wiring (Phases 0-3) covers every user-visible scenario discussed during scoping. In-content shader transitions are extra polish on top of motion that already plays — they're sequencing a *second* visual layer on a state change that doesn't itself enter or exit the screen. The visual win is real but small relative to the implementation cost (a new sibling QML library).

The unlanded work below is sized at ~3 days and tracked separately:

1. Spin up `phosphor-animation-shaders-qml` library mirroring `phosphor-animation-qml`'s CMake structure (STATIC + SHARED dual-target install dance).
2. Add `PhosphorShaderTransition` element — `Behavior`-style, resolves a `ShaderProfileTree` for a given path and drives a child `PhosphorRendering::ShaderEffect` over the same time window as the sibling motion Behavior.
3. Register via `QML_NAMED_ELEMENT` under a new module URI (likely `org.phosphor.animation.shaders` to keep `phosphor-animation-qml` Qt6::Qml-only as today).
4. Pair it with existing motion Behaviors in the three files above; document the pairing convention in `phosphor-animation-shaders/README.md`.

Phase 4a (small): renaming `zone.flash` → `zone.layoutSwitchIn` in `ZoneOverlay.qml` ✅ done in this branch — even without the new QML element, the rename means the C++-side `slotApplyGeometriesBatch` shader (Phase 0a) and the QML-side flash now respond to the same path on the user's tree.

---

## Phase 5 — Workspace events (deferred / optional)

- `workspace.switchIn` / `workspace.switchOut` / `workspace.overview` are owned by stock KWin effects (`slide`, `desktopgrid`, `overview`). PlasmaZones doesn't connect to `EffectsHandler::desktopChanged`.
- Wiring would need a per-screen shader pass via `paintScreen`, not per-window `OffscreenEffect`.
- Skip for now. If users want this, mark as a v3.x stretch goal with a separate design doc.

---

## Test strategy

Run after every phase, in this order:

1. **Unit:** existing `tests/unit/test_shaderprofile*` and `test_shader_profile_tree*`. Add tests for:
   - `applySnapGeometry` resolves the path it was passed (Phase 0a regression net).
   - Live-reload re-applies new tree to `SurfaceAnimator::Config` (Phase 0b).
   - `ShaderProfile::parameters` lands as uniforms on both render paths (Phase 0c).

2. **Manual smoke test:** the dissolve-everywhere config:
   ```jsonc
   "shaderProfileTree": "{\"baseline\":{\"effectId\":\"dissolve\"},\"overrides\":[]}"
   ```
   - Snap window → dissolve (zone.snapIn) ✓
   - Open new window → dissolve (window.open) — Phase 1
   - Cycle layout → dissolve on each resnap (zone.layoutSwitchIn) — Phase 0a
   - Pull up snap-assist → overlay dissolves in (panel.popup) — Phase 3
   - OSD on layout switch → dissolves (osd.show/hide) — Phase 3
   - Drag/resize during snap → dissolve (window.resize / zone.snapResize) — Phase 1 + 0a

3. **Per-event override test:** assign different effect IDs per path to confirm path-routing works:
   ```jsonc
   "shaderProfileTree": "{\"baseline\":{},\"overrides\":[
     {\"path\":\"window.open\",\"profile\":{\"effectId\":\"popin\"}},
     {\"path\":\"window.close\",\"profile\":{\"effectId\":\"dissolve\"}},
     {\"path\":\"zone.snapIn\",\"profile\":{\"effectId\":\"slide\"}},
     {\"path\":\"osd.show\",\"profile\":{\"effectId\":\"morph\"}}
   ]}"
   ```

4. **Live-reload test:** edit the tree while the daemon is running, change effect IDs, observe that subsequent triggers use the new effects without restart.

---

## Known limitations to land alongside

Document these in code (not just commit messages):

- Per-window event conflict (Phase 0d — last-event-wins) at `kwin-effect/plasmazoneseffect.h:507` near `m_shaderTransitions`.
- Uniform-contract divergence between kwin-effect and daemon paths (only `iTime` + `iResolution` are guaranteed shared) — document at `data/animations/README.md` if it exists, or in `libs/phosphor-animation-shaders/include/.../AnimationShaderEffect.h`.
- Workspace events deferred to v3.x — note in `ProfilePaths.h:46-67` workspace section.

---

## Effort estimate

Rough sizing:

- Phase 0a: ~1 day. Mechanical, well-bounded.
- Phase 0b: ~1.5 days. Crosses kwin-effect + daemon, needs careful test of mid-animation re-registration.
- Phase 0c: ~1 day. Uniform plumbing on two paths.
- Phase 0d: ~0.5 day for option A (document); option B is a multi-day refactor — skip unless needed.
- Phase 1: ~2 days. 7 call sites + helper + per-event durations + tests.
- Phase 2: rolls into 0a.
- Phase 3: ~1.5 days. 4 builder updates + injection point refactor + connect.
- Phase 4: ~3 days. New QML element + module updates + 3 site updates + tests. Defer.
- Phase 5: deferred.

In-scope total (Phases 0–3): **~7 days** sequential, **~4 days** with parallelism between kwin-effect (0a, 1) and daemon (0b daemon-side, 3) tracks.

---

## Branching / shipping

Land each phase as its own commit on `feat/animation-shader-wireup`. Open a single PR against `v3` after Phase 3 lands; Phase 4 follows in a separate PR.

Suggested commit titles (conventional):

- `fix(kwin-effect): pass profilePath into applySnapGeometry instead of hardcoding zone.snapIn`
- `feat(animation): live-reload shaderProfileTree on settingsChanged (kwin-effect + daemon)`
- `feat(animation): deliver ShaderProfile::parameters as GLSL uniforms on both render paths`
- `feat(kwin-effect): wire window-lifecycle paths (open/close/min/max/focus/move/resize) to ShaderProfileTree`
- `feat(daemon): wire OverlayService surface roles to ShaderProfileTree (osd.show/hide, panel.popup, etc.)`

---

## Open questions before kickoff

1. **Phase 0d option A vs B** — accept last-event-wins, or invest in per-event timelines? Default A.
2. **`workspace.*` priority** — punt to v3.x (default) or in-scope?
3. **`zone.flash` custom path in `ZoneOverlay.qml:274`** — keep as a leaf override on `zone.layoutSwitchIn` or rename to match the taxonomy? Default: rename + add migration note in commit.
4. **Phase 4 timing** — wait until Phases 0–3 ship and merge, or ship Phase 4 in the same PR?

Approve the plan as written or call out which questions you want to answer before kickoff.
