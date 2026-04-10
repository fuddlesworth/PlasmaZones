# Animation System Overhaul — Implementation Plan

## Context

Discussion #240 proposes an **Extended Window Animation System** combining niri's spring
physics + GLSL shader animations with Hyprland's hierarchical animation event tree.

The current system has a flat, global animation config (one duration, one easing curve,
one sequence mode applied to all snap events). This plan evolves it into a per-event
animation tree with spring physics as an alternative timing mode, built-in shader-based
animation styles, and custom GLSL shader support.

### Research Summary

| Feature | Niri | Hyprland | PlasmaZones (proposed) |
|---------|------|----------|----------------------|
| Timing modes | Easing OR Spring (per-event) | Bezier only | Easing OR Spring (per-event) |
| Event hierarchy | Flat (11 independent slots) | Tree with inheritance | Tree with inheritance |
| Easing curves | 5 built-in + cubic-bezier | User-defined bezier only | 12 presets + cubic-bezier + elastic + bounce |
| Spring physics | Damped harmonic oscillator (damping, stiffness, epsilon) | None | Same as niri |
| Animation styles | None (shaders handle visuals) | popin, slide, slidefade, fade, gnome | morph, slide, popin, slidefade, none, custom |
| Custom shaders | GLSL inline (open/close/resize only) | Screen shader only (post-process) | GLSL per-event (reuses ZoneShaderNodeRHI pipeline) |
| Per-window rules | No | Yes (noAnim rule) | Yes (app rules integration) |
| Global speed | slowdown multiplier | speed field per-event | Global multiplier + per-event duration |

---

## Phase 1: Settings UI + Config Infrastructure + Spring Physics

### 1A — Settings UI: Animations as Drilldown Menu

**Move Animations out of GeneralPage into its own top-level sidebar item with children.**

#### Sidebar Structure

```
Animations  [drilldown arrow]
  ├── General          — Global enable, default timing, speed multiplier, sequencing
  ├── Zone Snap        — zoneSnapIn / zoneSnapOut / zoneSnapResize overrides
  │                      (divider)
  ├── Fades            — fadeZoneHighlight / fadeOsd / fadeDim / fadeSwitch
  ├── Layout Switch    — layoutSwitchIn / layoutSwitchOut
  ├── Border           — Border animation
  ├── Preview          — Snap-assist preview animation
  │                      (divider)
  └── Shaders          — Custom shader selection per event (Phase 2 stub)
```

#### Files to Create (QML)

| File | Purpose |
|------|---------|
| `AnimationsGeneralPage.qml` | Global animation settings (migrated from GeneralPage) |
| `AnimationsSnapPage.qml` | Zone snap event overrides |
| `AnimationsFadePage.qml` | Fade event overrides |
| `AnimationsLayoutPage.qml` | Layout switch event overrides |
| `AnimationsBorderPage.qml` | Border animation settings |
| `AnimationsPreviewPage.qml` | Preview/snap-assist animation |
| `AnimationsShadersPage.qml` | Shader selection per event (Phase 2 shell) |
| `SpringPreview.qml` | Spring physics ball-on-spring visualization |
| `AnimationEventCard.qml` | Reusable card for per-event config |

#### Main.qml Changes

Add to `_mainItems` (between tiling and exclusions):
```js
{ "name": "animations", "label": i18n("Animations"),
  "iconName": "media-playback-start", "hasChildren": true, "hasDividerAfter": true }
```

Add to `_childItems`:
```js
"animations": [
  { "name": "animations-general",  "label": i18n("General"),       "iconName": "configure",                   "hasDividerAfter": true },
  { "name": "animations-snap",     "label": i18n("Zone Snap"),     "iconName": "view-split-left-right" },
  { "name": "animations-fade",     "label": i18n("Fades"),         "iconName": "gradient" },
  { "name": "animations-layout",   "label": i18n("Layout Switch"), "iconName": "view-grid" },
  { "name": "animations-border",   "label": i18n("Border"),        "iconName": "draw-rectangle" },
  { "name": "animations-preview",  "label": i18n("Preview"),       "iconName": "view-preview",                "hasDividerAfter": true },
  { "name": "animations-shaders",  "label": i18n("Shaders"),       "iconName": "preferences-desktop-effects" }
]
```

Add to `_pageComponents`:
```js
"animations-general":  "AnimationsGeneralPage.qml",
"animations-snap":     "AnimationsSnapPage.qml",
"animations-fade":     "AnimationsFadePage.qml",
"animations-layout":   "AnimationsLayoutPage.qml",
"animations-border":   "AnimationsBorderPage.qml",
"animations-preview":  "AnimationsPreviewPage.qml",
"animations-shaders":  "AnimationsShadersPage.qml"
```

Remove Animations card from `GeneralPage.qml` (lines 33-85).

#### Per-Event Override UX: Compact Cards + Popup Dialog

Override cards are kept **compact** — each shows a mini curve thumbnail (~80x50px),
timing mode, style, duration, and curve preset combo boxes. For full curve editing
(drag handles, spring parameter sliders, amplitude/period/bounces), a **"Customize
Curve..." button** opens a `CurveEditorDialog` popup.

**Why popup instead of inline**: The full bezier editor with drag handles is ~240px
tall plus controls. Embedding it in every override card would create enormous,
repetitive pages. The popup gives full editing power per-event while keeping cards
scannable.

New components:
- `CurveEditorDialog.qml` — `Kirigami.Dialog` wrapping full `EasingPreview`/`SpringPreview`
- `CurveThumbnail.qml` — small read-only Canvas rendering current curve (clickable)
- `AnimationEventCard.qml` — reusable override card with compact controls + popup button

See SVG mockups: `docs/media/mockups/`

---

### 1B — Animation Event Tree Config

#### Event Tree Hierarchy

```
global
  ├── zoneSnap
  │     ├── zoneSnapIn
  │     ├── zoneSnapOut
  │     └── zoneSnapResize
  ├── fade
  │     ├── fadeZoneHighlight
  │     ├── fadeOsd
  │     ├── fadeDim
  │     └── fadeSwitch
  ├── layoutSwitch
  │     ├── layoutSwitchIn
  │     └── layoutSwitchOut
  ├── border
  └── preview
```

Each node stores an `AnimationProfile` — empty/null fields inherit from parent.

#### New C++ Types — `src/core/animationprofile.h`

```cpp
namespace PlasmaZones {

struct SpringAnimation {
    qreal dampingRatio = 1.0;   // 0.1-10.0 (< 1 = bouncy, 1 = critical, > 1 = overdamped)
    qreal stiffness = 800.0;    // 1-2000
    qreal epsilon = 0.0001;     // 0.00001-0.1 (convergence threshold)
};

enum class AnimationStyle { Morph, Slide, Popin, SlideFade, None, Custom };

struct AnimationProfile {
    std::optional<bool> enabled;
    std::optional<int> timingMode;           // 0 = easing, 1 = spring
    std::optional<int> duration;             // ms (ignored for spring)
    std::optional<QString> easingCurve;      // bezier or named curve string
    std::optional<SpringAnimation> spring;
    std::optional<AnimationStyle> style;
    std::optional<qreal> styleParam;         // e.g. minScale for popin
    std::optional<QString> shaderPath;       // for AnimationStyle::Custom
    std::optional<QVariantMap> shaderParams; // uniform overrides
};

} // namespace PlasmaZones
```

#### AnimationProfileTree — `src/core/animationprofiletree.h`

```cpp
class AnimationProfileTree {
public:
    AnimationProfile resolvedProfile(const QString& eventName) const;
    void setProfile(const QString& eventName, const AnimationProfile& profile);
    AnimationProfile rawProfile(const QString& eventName) const;

    QJsonObject toJson() const;
    static AnimationProfileTree fromJson(const QJsonObject& obj);

    static QString parentOf(const QString& eventName);
    static QStringList childrenOf(const QString& eventName);
    static QStringList allEventNames();
};
```

#### Config Structure (`config.json`)

```json
{
  "AnimationProfiles": {
    "global": {
      "enabled": true,
      "timingMode": 0,
      "duration": 300,
      "easingCurve": "0.33,1.00,0.68,1.00",
      "style": "morph"
    },
    "zoneSnapIn": { "style": "slide" },
    "zoneSnapOut": { "style": "popin", "styleParam": 0.87 },
    "fade": { "duration": 200 }
  }
}
```

Empty/missing nodes inherit everything from parent. Partial objects inherit unset fields.

#### Settings Integration

- `configkeys.h` — new `animationProfilesGroup()` + per-field key accessors
- `configdefaults.h` — default `AnimationProfileTree`, spring preset defaults
- `interfaces.h` — `animationProfileTree()` / `setAnimationProfileTree()` / signal
- `settings.h/cpp` — Q_PROPERTY, load/save
- `settingscontroller.h/cpp` — expose resolved profiles + spring bounds to QML
- Existing flat settings become aliases for `global` node (backwards compatible)

---

### 1C — Spring Physics

#### SpringAnimation in WindowAnimator (`kwin-effect/windowanimator.h`)

```cpp
struct SpringAnimation {
    qreal dampingRatio = 1.0;
    qreal stiffness = 800.0;
    qreal epsilon = 0.0001;
    qreal velocity = 0.0;      // initial velocity (gesture input)

    qreal evaluate(qreal t) const;    // position at time t (seconds)
    bool isSettled(qreal t) const;     // converged within epsilon?
    qreal estimatedDuration() const;   // estimated settle time
};
```

Physics: damped harmonic oscillator
```
x(t) = e^(-zeta*omega*t) * (A*cos(omega_d*t) + B*sin(omega_d*t))
where omega = sqrt(stiffness), zeta = dampingRatio, omega_d = omega*sqrt(1-zeta^2)
```

`WindowAnimation::timing` becomes `std::variant<EasingCurve, SpringAnimation>`.

#### Default Presets

| Preset | Damping | Stiffness | Epsilon | Character |
|--------|---------|-----------|---------|-----------|
| Snappy | 0.8 | 800 | 0.001 | Fast with slight bounce |
| Smooth | 1.0 | 400 | 0.0001 | Critically damped, no overshoot |
| Bouncy | 0.5 | 600 | 0.001 | Playful oscillation |

---

## Files Modified

| File | Change |
|------|--------|
| `src/settings/qml/Main.qml` | Add animations drilldown |
| `src/settings/qml/GeneralPage.qml` | Remove animations card |
| `src/config/configkeys.h` | Add AnimationProfiles group + keys |
| `src/config/configdefaults.h` | Add profile defaults, spring defaults |
| `src/core/interfaces.h` | Add animationProfileTree to ISettings |
| `src/settings/settings.h` | Q_PROPERTY for profile tree |
| `src/settings/settings.cpp` | Load/save profiles |
| `src/settings/settingscontroller.h/cpp` | Expose to QML |
| `kwin-effect/windowanimator.h` | Add SpringAnimation, variant timing |
| `kwin-effect/windowanimator.cpp` | Spring physics evaluation |
| `src/settings/CMakeLists.txt` | Register new QML files |
| `src/core/CMakeLists.txt` | Add animationprofile.cpp |

---

## Verification

1. `cmake --build build --parallel $(nproc)`
2. `cd build && ctest --output-on-failure`
3. Launch plasmazones-settings — verify drilldown navigation
4. Verify per-event overrides with "Inherit from parent" defaults
5. Verify spring/easing toggle switches preview components
6. Verify config persistence across restarts

---

## Phase 2: Per-Event Wiring + Animation Styles + Shaders

### 2A — Wire Profile Tree into WindowAnimator

The profile tree is config/UI only in Phase 1. Phase 2 connects it to the runtime:

- Daemon resolves the per-event profile (e.g., `zoneSnapIn`) when starting an animation
- Pass resolved `AnimationProfile` into `WindowAnimator::startAnimation()` so it uses
  the correct timing (easing or spring), duration, and style per event
- The daemon must know *which event* is happening (snap-in vs snap-out vs resize vs
  fade, etc.) and look up the tree
- Consolidate flat settings (`animationEasingCurve`, `animationDuration`) into the
  profile tree as the single source of truth — remove sync hacks

### 2B — Animation Styles (Shader Render Path)

`AnimationStyle` enum exists (Morph/Slide/Popin/SlideFade/None/Custom) but only Morph
is implemented in `applyTransform()`.

- **AnimationShaderManager** — manages GLSL shaders for non-morph styles
- Built-in shaders:
  - Slide — translate window texture by direction vector
  - Popin — scale up from center + opacity fade
  - SlideFade — partial translate + alpha blend
- Integrate with existing `ZoneShaderNodeRHI` pipeline
- `applyTransform()` dispatches based on resolved style
- Shader uniforms: `pz_progress`, `pz_window_tex`, `pz_source_rect`, `pz_target_rect`

### 2C — Custom GLSL Shaders

- Populate `AnimationsShadersPage.qml` (currently a stub)
- Per-event shader selection from profile tree's `shaderPath` + `shaderParams`
- Shader authoring/selection UI
- Shader entry points: `snap_color`, `leave_color`, etc.

### 2D — Gesture Velocity → Spring Physics

- `SpringAnimation::initialVelocity` exists but is always `0.0`
- Wire gesture release velocity into the spring so animations feel physically
  connected to the drag

---

## Phase 3: Global Window Animations

Expand PlasmaZones from snapping-only animations into a general-purpose KWin window
animation engine, replacing or augmenting KWin's built-in effects.

### 3A — New Window Lifecycle Events

Hook into KWin effect signals for all major window state transitions:

| Event | KWin Signal | Animation |
|-------|-------------|-----------|
| Window open | `windowAdded` | Fade/scale in from nothing |
| Window close | `windowClosed` | Fade/scale out to nothing |
| Minimize | `windowMinimized` | Scale down to taskbar |
| Unminimize | `windowUnminimized` | Scale up from taskbar |
| Maximize | `windowMaximizeStateChanged` | Geometry morph (non-snap) |
| Unmaximize | `windowMaximizeStateChanged` | Geometry morph back |
| Desktop switch | `desktopChanged` | Slide/fade desktop transition |
| Focus change | `windowActivated` | Subtle scale/opacity pulse |

### 3B — Expanded Profile Tree

```
global
  ├── zoneSnap → zoneSnapIn, zoneSnapOut, zoneSnapResize
  ├── fade → fadeZoneHighlight, fadeOsd, fadeDim, fadeSwitch
  ├── layoutSwitch → layoutSwitchIn, layoutSwitchOut
  ├── border
  ├── preview
  ├── windowOpen
  ├── windowClose
  ├── minimize → minimizeIn, minimizeOut
  ├── maximize → maximizeIn, maximizeOut
  ├── desktopSwitch
  └── focusChange
```

All new nodes inherit from `global` and support the full profile (timing mode,
easing/spring, duration, style, shader).

### 3C — Per-Window Animation Overrides (App Rules)

Integrate with existing app rules system:

- `skipAnimation: bool` — disable all animations for matched windows
- `animationProfile: string` — override event name to use (e.g., force spring
  physics for a specific app's open/close)
- `animationStyle: string` — override style per window class
- Per-window rules apply on top of the profile tree's per-event resolution

### 3D — KWin Effect Coordination

- Detect and disable conflicting KWin built-in effects (wobbly windows, magic lamp,
  desktop slide) when PlasmaZones handles the same event
- Provide a "managed effects" settings section listing which window events PlasmaZones
  handles vs delegates to KWin
- Fallback: if PlasmaZones effect is unloaded, KWin's built-in effects resume

### 3E — Settings UI

- New sidebar pages under Animations for lifecycle events (Open/Close, Minimize,
  Maximize, Desktop Switch, Focus)
- Per-window animation overrides in the existing App Rules page
- "Managed effects" toggle list showing PlasmaZones vs KWin ownership per event
