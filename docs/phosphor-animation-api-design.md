# PhosphorAnimation — API Design Document

## Overview

PhosphorAnimation is the motion-runtime library for Phosphor-based shells,
compositors, and window managers. It owns the curve math, spring physics,
per-event configuration, and orchestration primitives that every UI /
compositor animation in PlasmaZones eventually flows through.

Scope is deliberately broader than "easing math": the ambition is
niri / Hyprland / Quickshell-level customization, which means the API
must support user-defined curve types at runtime, physics-backed motion
with velocity continuity, and a hierarchical per-event configuration
model that plugins can extend without a library change.

**License:** LGPL-2.1-or-later
**Namespace:** `PhosphorAnimation`
**Depends on:** Qt6::Core, Qt6::Gui
**Build artefact:** `libPhosphorAnimation.so` (SHARED)

---

## Dependency Graph

```
                     PhosphorAnimation (SHARED, Qt6::Core + Qt6::Gui)
                                    │
        ┌──────────────┬────────────┼──────────────┬───────────────┐
        │              │            │              │               │
  kwin-effect      daemon       settings UI    compositor      phosphor-layer
  (WindowAnimator, (snap        (EasingSettings plugin SDK     (ISurfaceAnimator
   stagger         animation    QML → runtime   [future]         impl, Phase 5)
   timer,          config       Profile)
   Easing          flow)
   parsing)
```

Every consumer links `PhosphorAnimation::PhosphorAnimation`. Qt6::Gui is
required for `QPointF` / `QSizeF` / `QRect` / `QRectF` used throughout
`WindowMotion` + `AnimationMath`; the library pulls in no Qt Quick / QML
/ Wayland / KWin dependencies at this layer.

---

## Design Principles

1. **Polymorphic curve dispatch, not `std::variant`.** The curve hierarchy
   is open. Third parties register new curve types at runtime via
   `CurveRegistry`. A closed variant would require a library recompile
   every time a plugin wants a wavetable / physics-with-extras / user-
   drawn curve — that's incompatible with niri / Hyprland / Quickshell
   customization.

2. **Immutable curves, value-shared via `shared_ptr<const Curve>`.**
   Construction-time parameter validation; no further mutation. Callers
   hold `shared_ptr<const Curve>` — cheap copy, safe across threads,
   multiple `AnimatedValue` / `WindowMotion` / `Profile` instances can
   reference the same curve definition. Concrete subclasses (`Easing`,
   `Spring`) also keep value-copy semantics for legacy call sites that
   hold curves by value.

3. **Stable string round-trip for every curve.**
   `curve → toString() → CurveRegistry::create() → curve'` is the
   canonical serialization path. Config files, D-Bus, settings UIs, and
   user scripts all interoperate through strings. Custom curve types
   registered at runtime get the same treatment — no special-case
   serialization.

4. **Parametric + stateful evaluation coexist.** `evaluate(t)` returns
   the curve's value at normalized time `t ∈ [0, 1]`; `step(dt, state,
   target)` advances a `CurveState` by real-time `dt` with velocity
   continuity. Parametric is the default for fixed-duration animations;
   stateful is what `Spring` requires for retarget-mid-flight without
   visible discontinuities. Every curve provides both, with sensible
   defaults for stateless subclasses.

5. **Shell event taxonomy is data, not an enum.** `ProfileTree` is keyed
   by dot-path strings (`window.open`, `zone.snapIn`, `osd.show`). The
   library ships a canonical taxonomy in `ProfilePaths` but does not
   enforce it — plugins add `widget.toast.slideIn` without library
   changes, and the inheritance walk (`window.open` → `window` →
   `global`) respects their paths naturally.

6. **Compositor-agnostic.** No references to KWin, Wayland, X11, or
   any window-system. `WindowMotion` describes geometry transitions in
   pure `QPointF` / `QSizeF` / `QRect` terms. The KWin-specific paint
   glue lives in `kwin-effect/windowanimator.cpp`; future compositor
   plugins will ship their own adapters against the same state model.

7. **String + JSON are the wire formats.** No QDataStream, no QDBusArgument
   at the curve / profile layer. Callers that need D-Bus marshalling
   pass the string form through.

8. **No QObject, no signals at the data layer.** `Curve` / `Profile` /
   `ProfileTree` / `WindowMotion` are plain data types. `StaggerTimer`
   uses `QObject` only as a lifetime-context parent for
   `QTimer::singleShot` — the functions themselves are free. Observers
   wrap these types externally when Qt property binding / signal
   dispatch is needed.

---

## Public API

The complete public API, by header. Every type is in namespace
`PhosphorAnimation`.

### `Curve.h` — polymorphic base

```cpp
struct CurveState {
    qreal value = 0.0;     // current value (overshoot allowed)
    qreal velocity = 0.0;  // rate of change per second (springs)
    qreal time = 0.0;      // elapsed seconds since start
};

class Curve {
public:
    virtual ~Curve() = default;

    virtual QString typeId() const = 0;
    virtual qreal evaluate(qreal t) const = 0;
    virtual void step(qreal dt, CurveState& state, qreal target) const;
    virtual bool isStateful() const { return false; }
    virtual qreal settleTime() const { return 1.0; }
    virtual QString toString() const = 0;
    virtual std::unique_ptr<Curve> clone() const = 0;
    virtual bool equals(const Curve& other) const;

protected:
    Curve() = default;                      // protected copy/move —
    Curve(const Curve&) = default;          // prevents slicing outside
    Curve& operator=(const Curve&) = default; // the hierarchy while
    Curve(Curve&&) = default;               // allowing subclasses to
    Curve& operator=(Curve&&) = default;    // default their own.
};
```

Design notes:
- `evaluate(t)` and `step(dt, state, target)` are peers, not alternatives.
  Parametric curves use `evaluate`; stateful curves override both.
- `isStateful()` is a hint, not a contract — `AnimatedValue` (Phase 3)
  uses it to decide whether to call `step` or fall through to `evaluate`.
- `equals()` default is `typeId() == other.typeId() && toString() ==
  other.toString()`. Subclasses with non-lossless string forms override.
- Protected copy is the Scott Meyers idiom for polymorphic value types:
  subclasses `= default` their own copy to preserve value semantics,
  slicing through `Curve` base is prevented.

### `Easing.h` — concrete curve (cubic-bezier + elastic + bounce)

Seven variants via an internal `enum class Type`: `CubicBezier`,
`ElasticIn/Out/InOut`, `BounceIn/Out/InOut`. Public parameter fields
(`x1/y1/x2/y2` for bezier; `amplitude`, `period`, `bounces` for elastic /
bounce) are public — `Easing` is a value-type aggregate. Ranges are
clamped on parse (`x ∈ [0,1]`, `y ∈ [-1,2]`, `amplitude ∈ [0.5, 3.0]`,
`period ∈ [0.1, 1.0]`, `bounces ∈ [1, 8]`).

Wire formats — exactly one per curve type, no parallel encodings:
- Bezier:  `"0.33,1.00,0.68,1.00"` (four comma-separated floats, no prefix)
- Elastic: `"elastic-out:1.0,0.3"` (amplitude, period)
- Bounce:  `"bounce-out:1.5,4"` (amplitude, bounces)

`fromString` and `toString` are inverses on these formats. The
`"bezier:..."` prefixed form is intentionally NOT accepted — there is
one wire format per curve type.

### `Spring.h` — damped harmonic oscillator

Parameterized as `(omega, zeta)`:
- `omega` (ω₀): angular natural frequency in rad/s, clamped `[0.1, 200]`
- `zeta` (ζ): damping ratio, clamped `[0.0, 10.0]`
  - `ζ < 1`: underdamped (bouncy, overshoots)
  - `ζ = 1`: critically damped (fastest non-oscillatory approach)
  - `ζ > 1`: overdamped (slow, no overshoot)

Evaluation models:
- `evaluate(t)`: analytical step-response, domain `t ∈ [0, 1]` mapped
  to real time `[0, settleTime()]`. Overshoot preserved by design
  (underdamped curves exceed 1.0 mid-flight).
- `step(dt, state, target)`: semi-implicit (symplectic) Euler. Stable
  for `dt < 1 / (5·omega)` — at `omega=30` that's ~6.6 ms, below the
  16 ms frame budget at 60 Hz. Preserves velocity across calls, which
  is what retarget-mid-flight needs to avoid stall discontinuities.
- `settleTime()`: analytical 2% settling, capped at 30 s.
- Convergence lock: `step()` snaps to target and zeroes velocity once
  both are under configured epsilons (`1e-4` value, `1e-3` velocity).
  Prevents infinitesimal drift accumulating across thousands of frames.

Presets: `Spring::snappy()`, `smooth()`, `bouncy()`.

Serialized form: `"spring:12.00,0.80"` (omega, zeta). `fromString`
accepts with or without the `spring:` prefix for symmetry with the
legacy bezier form.

### `CurveRegistry.h` — string-id ↔ factory

Singleton (Meyers `static`) registry mapping `typeId → Factory`. Built-
in registrations for all 7 easing variants + `spring`. Thread-safe via
`QMutex`.

```cpp
using Factory = std::function<std::shared_ptr<const Curve>(
    const QString& typeId, const QString& params)>;

class CurveRegistry {
public:
    static CurveRegistry& instance();
    bool registerFactory(const QString& typeId, Factory factory);
    bool unregisterFactory(const QString& typeId);
    std::shared_ptr<const Curve> create(const QString& spec) const;
    QStringList knownTypes() const;
    bool has(const QString& typeId) const;
};
```

Parsing contract in `create(spec)`:
- `"typeId:params"` → dispatch to registered factory for `typeId`.
- `"x1,y1,x2,y2"` (four commas, no colon) → legacy bezier.
- `"typeId"` (bare, no params) → factory with empty params.
- Empty → returns `nullptr` (caller chooses fallback).
- Unknown `typeId` → returns a default `Easing` (outCubic) and logs a
  warning. Never returns null for non-empty input.

Ordering: `knownTypes()` returns insertion order. Snapshot tests rely
on this; Q_GLOBAL_STATIC was avoided in favor of a Meyers singleton to
dodge static-destruction ordering pitfalls with Qt plugin teardown.

### `Profile.h` — single-event configuration

```cpp
enum class SequenceMode : int {
    AllAtOnce = 0,
    Cascade = 1,
};

class Profile {
public:
    static constexpr qreal DefaultDuration = 150.0;
    static constexpr int DefaultMinDistance = 0;
    static constexpr SequenceMode DefaultSequenceMode = SequenceMode::AllAtOnce;
    static constexpr int DefaultStaggerInterval = 30;

    std::shared_ptr<const Curve> curve;         // null = inherit
    std::optional<qreal> duration;              // unset = inherit
    std::optional<int> minDistance;             // unset = inherit
    std::optional<SequenceMode> sequenceMode;   // unset = inherit
    std::optional<int> staggerInterval;         // unset = inherit
    QString presetName;                         // "" = unset

    qreal effectiveDuration() const;
    int effectiveMinDistance() const;
    SequenceMode effectiveSequenceMode() const;
    int effectiveStaggerInterval() const;
    Profile withDefaults() const;

    QJsonObject toJson() const;
    static Profile fromJson(const QJsonObject& obj);
    bool operator==(const Profile& other) const;
};
```

Every field uses `std::optional` (or a nullable `shared_ptr` for the
curve) to distinguish "I didn't say" from "I explicitly set the
default value". Sentinel-based inheritance — e.g. "treat `staggerInterval
== 30` as unset" — is fundamentally broken for any override that wants
to force a child back to the library default even though the parent
differs. A leaf with `duration = 150` MUST win over a parent with
`duration = 300`, and only an optional can express that.

JSON serialization omits unset fields and the empty `presetName`. A
`duration` field explicitly set to the library default is still written
out so the override survives round-trip. `SequenceMode` is a strongly
typed enum; unknown integer values in JSON clamp to the default
enumerator for forward-compatibility.

### `ProfilePaths.h` — named event path constants

37 constants organized into 7 categories plus `Global` root:

| Category    | Paths                                                          |
|-------------|----------------------------------------------------------------|
| `global`    | (root)                                                         |
| `window.*`  | open, close, minimize, unminimize, maximize, unmaximize,       |
|             | move, resize, focus                                            |
| `zone.*`    | snapIn, snapOut, snapResize, highlight, layoutSwitchIn,        |
|             | layoutSwitchOut                                                |
| `workspace.*` | switchIn, switchOut, overview                                |
| `osd.*`     | show, hide, dim                                                |
| `panel.*`   | slideIn, slideOut, popup                                       |
| `cursor.*`  | hover, click, drag                                             |
| `shader.*`  | open, close, switch                                            |

Plus `QStringList allBuiltInPaths()` for settings-UI enumeration and
`QString parentPath(const QString&)` for inheritance walks. Extension
paths added by plugins are NOT enumerated here — callers enumerate the
`ProfileTree` itself if they need the full set.

### `ProfileTree.h` — hierarchical overrides with inheritance

Sparse `QHash<QString, Profile>` of explicit overrides plus a single
baseline Profile. Insertion order is preserved for deterministic
iteration and UI display.

```cpp
class ProfileTree {
public:
    Profile resolve(const QString& path) const;  // walk-up inheritance
    Profile directOverride(const QString& path) const; // direct, no walk
    bool hasOverride(const QString& path) const;
    QStringList overriddenPaths() const;

    void setOverride(const QString& path, const Profile& profile);
    bool clearOverride(const QString& path);
    void clearAllOverrides();

    Profile baseline() const;
    void setBaseline(const Profile& profile);

    QJsonObject toJson() const;
    static ProfileTree fromJson(const QJsonObject& obj);
};
```

Resolution semantics: `resolve(path)` walks `path` up through parent
segments (via `ProfilePaths::parentPath`), starts from the baseline,
and overlays each explicit override in root-to-leaf order. At each
overlay, only the override's **engaged** optionals replace the
accumulator; `std::nullopt` fields are skipped and the accumulator is
left alone. After the chain walk, any still-unset field is filled from
the library defaults, so the returned Profile is always fully
populated. A leaf override that only sets `duration` inherits `curve`,
`staggerInterval`, etc. from the category or baseline; a leaf override
that sets `duration = 150` (the library default) correctly wins over a
parent with `duration = 300`.

JSON shape uses an **array** for overrides (not an object) because
`QJsonObject` alphabetically sorts keys on serialization, which would
silently reshuffle user-visible UI ordering:

```json
{
  "baseline": { ... profile ... },
  "overrides": [
    { "path": "window",      "profile": { ... } },
    { "path": "zone.snapIn", "profile": { ... } }
  ]
}
```

`fromJson` accepts the canonical array shape only. There is no
speculative legacy-object fallback: the library is brand new and has no
prior persisted format, so per CLAUDE.md's "no ad-hoc migration code"
rule, reads and writes share the same shape.

### `WindowMotion.h` — per-window snap animation state

```cpp
struct WindowMotion {
    QPointF startPosition;
    QSizeF  startSize;
    QRect   targetGeometry;
    std::chrono::milliseconds startTime{-1};  // -1 = pending
    qreal duration = 150.0;
    Easing easing;
    qreal cachedProgress = 0.0;

    bool isValid() const;
    void updateProgress(std::chrono::milliseconds presentTime);
    qreal progress() const;
    bool isComplete(std::chrono::milliseconds presentTime) const;
    QPointF currentVisualPosition() const;
    QSizeF  currentVisualSize() const;
    bool hasScaleChange() const;
};
```

Progress semantics:
- `startTime < 0` is pending; first `updateProgress(presentTime)` call
  latches it. Zero-duration animations complete on the same call.
- `cachedProgress` is refreshed once per frame by `updateProgress`;
  `currentVisualPosition / currentVisualSize` read from the cache so
  position and size stay consistent within a paint cycle.
- Progress overshoot (elastic / bounce) is preserved — `progress()`
  can legitimately return 1.05 or -0.02.

Phase 2 note ✅: `WindowMotion::easing` was replaced by `curve`
(`std::shared_ptr<const Curve>`). No `CurveState` lives on the struct —
progression is parametric (`curve->evaluate(t)` populates `cachedProgress`
once per frame). The terminal frame snaps `cachedProgress` to exactly
`1.0` regardless of curve shape, so Spring-like curves whose
`evaluate(1)` lands within their settle band do not paint a visible miss
on the last frame. Stateful step-based progression + velocity-preserving
retarget are a Phase 3 concern (`AnimatedValue<T>` holds `CurveState`
alongside the clock; see Open Question 3 below).

### `AnimationMath.h`

```cpp
namespace AnimationMath {
    std::optional<WindowMotion>
    createSnapMotion(const QPointF& oldPosition, const QSizeF& oldSize,
                     const QRect& targetGeometry, qreal duration,
                     const Easing& easing, int minDistance);

    QRectF repaintBounds(const QPointF& startPos, const QSizeF& startSize,
                         const QRect& targetGeometry, const Easing& easing,
                         const QMarginsF& padding);
}
```

`createSnapMotion` returns `nullopt` for degenerate target geometry or
sub-threshold position delta without scale change. The caller owns the
"should I animate at all?" gate (e.g., a global enabled flag) — this
function only refuses on geometry grounds.
`repaintBounds` unions start + target rects with padding, and samples
the curve when overshoot is possible (elastic, bounce with amplitude
> 1, or bezier with out-of-range `y` controls) so repaint regions
don't under-invalidate during the middle of the animation.

### `StaggerTimer.h`

```cpp
void applyStaggeredOrImmediate(
    QObject* parent, int count, SequenceMode sequenceMode,
    int staggerInterval,
    const std::function<void(int)>& applyFn,
    const std::function<void()>& onComplete = nullptr);
```

Single free function, enum-typed for `sequenceMode`. `Cascade` with
`staggerInterval > 0` and `count > 1` cascades via
`QTimer::singleShot`; everything else runs synchronously. The parent
guard cancels pending fires if `parent` is destroyed mid-cascade —
critical for the effect-teardown path where windows may close while
their snap cascade is in flight. `onComplete` is **not** invoked on
cancellation; callers needing cleanup hooks must observe parent
destruction independently.

D-Bus / config call sites loading raw integers convert at the
boundary (the kwin-effect's `applyStaggeredOrImmediate` wrapper does
this once for the whole compositor effect).

Parent-guard caveat: Qt cancels the lambda when `parent` dies, but
objects captured *inside* `applyFn` / `onComplete` have their own
lifetime. The header documents the three acceptable patterns (capture
is `parent`, capture is owned by `parent`, or weak `QPointer` capture
with check).

---

## Design Rationale / Decision Log

**Polymorphic `Curve` vs `std::variant<Easing, Spring>`.** Variant is
lighter (stack-allocated, no vtable) but is a *closed set*. A shell
targeting niri / Hyprland / Quickshell ambitions needs runtime
extensibility: users define their own curves in config and the shell
looks them up by name. Variant forces a library recompile for every
new curve type. We pay one vtable lookup per `evaluate()` call in
exchange for open extension — at 60 fps per-window, that cost is
unmeasurable.

**Angular-frequency + damping-ratio vs mass/stiffness/damping.** The
physical parameterization (`m`, `k`, `c`) is expressive but unintuitive
for UI authors. Industry standard for UI motion libraries (Framer
Motion, SwiftUI, Apple UIKit spring animations) is `(omega, zeta)` or
the equivalent `(response, damping ratio)`. Easy to reason about:
"bigger omega = snappier", "zeta below 1 bounces, above 1 doesn't".
Converting from `(m, k, c)` is trivial if we ever need to expose both.

**Semi-implicit Euler for spring step().** Explicit Euler is unstable
for stiff springs at practical frame rates. RK4 is overkill and costs
4 force evaluations per step. Semi-implicit Euler (Symplectic Euler)
gives near-analytical stability up to `dt < 1 / (5·omega)` for a single
force evaluation, which is the sweet spot for 60 Hz UI animation.
Callers integrating at lower frame rates or with very stiff springs
can substep externally.

**Analytical `evaluate(t)` for springs.** For fixed-duration animations
that just want spring *feel* without driving true physics, callers
shouldn't need to integrate across frames. Shipping the closed-form
under/critically/over-damped step response makes springs a drop-in
replacement for easing in any context that already has a duration.

**Strings, not enums, for curve typeIds.** Enums break extensibility
at the ABI boundary. Strings round-trip cleanly through config files,
D-Bus, QML, and user scripts with no special case for library-internal
types versus plugin-defined types.

**Dot-path tree keys, not a fixed event enum.** Same reasoning. A fixed
`AnimationEvent` enum would force every shell extension that adds a new
animation site (notifications, tooltips, overview composer, custom
panel widgets) to PR the library. Dot paths let plugins define their
own namespaces without library involvement; the library just ships a
canonical taxonomy for common events.

**Array-shaped `overrides` JSON.** `QJsonObject` alphabetically sorts
keys at serialize time. For a settings UI that displays overrides in
user-added order, object-shaped storage silently reshuffles every
read. The array-of-objects form preserves order losslessly.

**Protected copy/move on `Curve`.** Scott Meyers's idiom for polymorphic
value types. Public copy would allow `Curve base = derivedInstance;`
slicing outside the hierarchy; deleted copy would forbid subclass
`= default` declarations. Protected gives subclasses opt-in value
semantics without enabling unsafe base-class copying.

**No legacy aliases / shims.** PlasmaZones is the only consumer of
this library today. We moved every call site to `PhosphorAnimation::`
in the same PR as the extraction rather than shipping
`PlasmaZones::EasingCurve` forwarders. If a third-party consumer
appears later, that's when we revisit deprecation aliases — not before.

**No QObject / signals at the data layer.** Profiles change rarely
(user opens Settings, edits a curve, closes). The cost of wiring every
`Profile` / `ProfileTree` as a QObject with signals was not justified
by the rate of change. Consumers that need reactive updates (the
settings UI, the daemon's curve-refresh path) wrap these types in a
QObject observer.

**Meyers singleton for `CurveRegistry`, not `Q_GLOBAL_STATIC`.**
Q_GLOBAL_STATIC destructs at shared-library teardown, which races with
Qt's plugin-teardown path. A function-local `static` constructed on
first access has well-defined lifetime (C++11 guarantees thread-safe
initialization) and survives until process exit.

---

## Non-Goals (for this library)

- **Animation driver / clock abstraction.** The Phase-3 `IMotionClock`
  hasn't landed; today `WindowMotion` takes `presentTime` directly. The
  abstraction cost wasn't justified until multiple compositors are
  consuming the library.
- **QML integration.** `PhosphorEasing` / `PhosphorSpring` /
  `PhosphorMotion` QML value types are Phase 4. The C++ runtime can
  land first; QML bindings come once the API has been exercised in
  kwin-effect and settings.
- **Shader-backed composite transitions.** Phase 6. Belongs in
  `phosphor-animation` (not `src/daemon/rendering/` or elsewhere) but
  requires phase-2 progression refactor first so shader effects can
  hang off `AnimationController`.
- **`ISurfaceAnimator` implementation.** Phase 5. `phosphor-layer`
  defines the interface; a real animator implementation for OSDs /
  overlays / snap-assist will ship as a sub-target that depends on
  both `phosphor-animation` and `phosphor-layer`.
- **`AnimatedValue<T>` template.** Phase 3. The current primitives
  work with concrete `QPointF` / `QSizeF` state inside `WindowMotion`
  because that's all the compositor side needs today. Generic
  `AnimatedValue<T>` for color / transform / scalar properties comes
  alongside QML integration.

---

## Roadmap — Phases 2 through 6

Each phase is independently shippable, independently reviewable, and
can be sequenced in any order that matches external priorities. Phase
numbering reflects dependency order, not required calendar order.

### Phase 2 — WindowAnimator split ✅ DONE

**Scope:** Pull the compositor-agnostic state machine out of
`kwin-effect/windowanimator.cpp` into a reusable
`PhosphorAnimation::AnimationController` base; `WindowAnimator` in
`kwin-effect/` becomes a thin KWin adapter.

**Landed:**
- `include/PhosphorAnimation/AnimationController.h` — header-only
  template `AnimationController<Handle>` holding `QHash<Handle,
  WindowMotion>`, configuration setters (enabled, duration, curve,
  minDistance), lifecycle methods (`startAnimation`, `removeAnimation`,
  `clear`, `hasAnimation`, `isAnimatingToTarget`, `currentVisualPosition
  / Size`, `motionFor`).
- `advanceAnimations(presentTime)` iterates, calls
  `WindowMotion::updateProgress`, invokes the four virtual hooks for
  subclasses (`onAnimationStarted`, `onAnimationComplete`,
  `onRepaintNeeded`, `isHandleValid`, `expandedPadding`).
- `WindowMotion::easing` (Easing value) renamed to `curve`
  (`std::shared_ptr<const Curve>`) — Spring + user-registered curves
  drive window motion via polymorphic `evaluate()`. Null curve =
  linear progression.
- `Curve::overshoots()` virtual added; Easing + Spring override.
  `AnimationMath::repaintBounds` uses it polymorphically (no more
  Easing-specific `type` switch in the bounds computation).
- `AnimationMath::createSnapMotion` + `repaintBounds` signatures take
  `shared_ptr<const Curve>`.
- `kwin-effect/WindowAnimator` is now `: public
  AnimationController<KWin::EffectWindow*>` with four hook overrides
  (`onAnimationStarted`, `onAnimationComplete`, `onRepaintNeeded`,
  `isHandleValid`, `expandedPadding`) plus the retained
  `applyTransform(EffectWindow*, WindowPaintData&)` paint coupling.
- `plasmazoneseffect.cpp` switched from `Easing::fromString` to
  `CurveRegistry::create` so Spring works end-to-end.
- 10 new `pa_test_animationcontroller` cases with a mock `int`-handle
  controller cover lifecycle, hook ordering, polymorphic curve
  dispatch, invalid-handle pruning, fallback queries, and bounds.

**Out:**
- `WindowAnimator` is no longer a `QObject` (it doesn't emit signals
  and the controller base is plain C++) — construction site changed
  from `make_unique<WindowAnimator>(this)` to `make_unique<WindowAnimator>()`.

### Phase 3 — IMotionClock + AnimatedValue<T>

**Scope:** Decouple progression from `presentTime` so QML OSDs and
compositor-side window motion share one clock model. Introduce a
generic `AnimatedValue<T>` template for non-geometry animations.

**Library additions:**
- `include/PhosphorAnimation/IMotionClock.h` — abstract clock with
  `std::chrono::milliseconds now() const` and an optional repaint
  request hook.
- `include/PhosphorAnimation/CompositorClock.h` — concrete impl fed
  by KWin presentTime (or equivalent on other compositors).
- `include/PhosphorAnimation/QtQuickClock.h` — concrete impl driven
  by `QQuickWindow::beforeRendering`.
- `include/PhosphorAnimation/AnimatedValue.h` — template holding
  `start`, `target`, current value, `CurveState`, clock reference,
  and a curve pointer. Methods: `start(from, to, profile)`,
  `retarget(newTo, policy)`, `cancel()`, `finish()`, `value()`,
  `velocity()`, `bounds()`.
- Specializations for `qreal`, `QPointF`, `QSizeF`, `QRectF`,
  `QColor`, `QTransform`.

**Retarget policy enum:**
```cpp
enum class RetargetPolicy {
    PreserveVelocity,   // default — no stall
    ResetVelocity,
    PreservePosition,
};
```

**Success criteria:**
- A QML-side animation and a compositor-side animation with the same
  `Profile` produce visually identical motion.
- Spring retarget-mid-flight test: snap A → B, midway issue retarget
  to C, verify no visual stall (velocity preserved by default).
- Overshoot bounds sampling generalized across parametric and
  stateful curves.

**Estimated scope:** ~700 LOC library, ~15 new unit tests.

### Phase 4 — QML integration

**Scope:** Expose the motion-runtime to QML so shell UI can bind
animations to user-configured profiles instead of hardcoded
`Easing.OutCubic`.

**Library additions:**
- `include/PhosphorAnimation/qml/PhosphorEasing.h` — Q_GADGET value
  type wrapping `Easing`, constructible from string.
- `include/PhosphorAnimation/qml/PhosphorSpring.h` — Q_GADGET for
  `Spring`.
- `include/PhosphorAnimation/qml/PhosphorCurve.h` — Q_GADGET opaque
  handle for `shared_ptr<const Curve>`, usable from QML as a curve
  reference.
- `include/PhosphorAnimation/qml/PhosphorMotion.h` — attached type
  for `Behavior { PhosphorMotion { profile: ... } }` sites.
- `include/PhosphorAnimation/qml/PhosphorAnimatedValue.h` — QML-
  facing wrapper around `AnimatedValue<T>` (Phase-3 dependency).

**Build additions:**
- QML plugin target `phosphor-animation-qml` (depends on
  `phosphor-animation` core + `Qt6::Quick`).
- Module URI `org.phosphor.animation`.

**Migration:**
- `src/settings/qml/EasingSettings.qml` binds real `Profile`
  instances; eliminates the duplicated bezier / elastic / bounce
  parser that currently mirrors the C++ `Easing::fromString`.
- `src/editor/qml/**`, `src/settings/qml/**`, `src/ui/**` animation
  sites migrate from hardcoded easings to theme-bound `PhosphorMotion`.

**Success criteria:**
- User-edited curve in Settings visibly affects zone-highlight fade,
  layout OSD, and snap animation *without* restarting the daemon.
- QML-only shell extensions (future Quickshell-style consumers) can
  author animations without touching C++.

**Estimated scope:** ~500 LOC library + QML plugin, ~200 LOC QML
migration, 10 new unit tests + integration smoke tests.

### Phase 5 — ISurfaceAnimator adapter

**Scope:** Ship a concrete `PhosphorLayer::ISurfaceAnimator`
implementation backed by `phosphor-animation`, so overlay surfaces
(OSD, snap-assist, layout picker, zone selector) share one
animation runtime.

**Build additions:**
- Optional CMake target `phosphor-animation-layer` (depends on
  `phosphor-animation` + `phosphor-layer`).
- `adapters/surface_animator.cpp` — implements `beginShow` / `beginHide`
  / `cancel` by driving an `AnimatedValue<qreal>` on root-item
  opacity + optional slide / scale transforms.

**Migration:**
- PlasmaZones `OverlayService` passes the new `SurfaceAnimator`
  instead of `NoOpSurfaceAnimator` to every
  `PhosphorLayer::SurfaceFactory::create()` call.

**Success criteria:**
- Overlay show/hide animations are driven by the same curve /
  duration config as in-window animations.
- `NoOpSurfaceAnimator` remains in `phosphor-layer` as the no-animation
  fallback for consumers that don't link `phosphor-animation`.

**Estimated scope:** ~200 LOC library, 4–5 new unit tests.

### Phase 6 — Shader-backed animation pipeline

**Scope:** Own the shader side of animated transitions inside
`phosphor-animation` rather than leaving it in
`src/daemon/rendering/`. Supersedes the ambition of PR #291 (which
predates the v3 refactors and needs to be gutted — only the Settings
UI bits are salvageable).

**Library additions:**
- `include/PhosphorAnimation/ShaderEffect.h` — animation-side shader
  definition (distinct from `phosphor-rendering`'s zone shaders).
  Metadata describes inputs (start texture, end texture, iTime,
  iFrame, audioSpectrum, etc.) + KWin / QtQuick variant selection.
- `include/PhosphorAnimation/ShaderRegistry.h` — discovery of
  `data/animations/*/metadata.json` shader packs; produces
  `ShaderEffect` instances addressable by id (`dissolve`, `glitch`,
  `morph`, `pixelate`, `popin`, `slide`, `slidefade`, + user packs).
- `include/PhosphorAnimation/ShaderProfile.h` — per-event shader
  selection layered on top of `Profile` (different events can use
  different shader effects).
- `src/daemon/rendering/animationshader*` equivalents migrate into
  the library as the shared runtime; daemon keeps only the QtQuick
  item integration.

**Build additions:**
- Asset pipeline: `data/animations/*/effect.frag` / `effect.vert` /
  `effect_kwin.frag` / `metadata.json` shipped via CMake install rules
  and consumed through `QStandardPaths`.
- Optional CMake target `phosphor-animation-shaders` (depends on
  `phosphor-animation` + `phosphor-rendering`).

**Migration:**
- KWin effect paints registered shader effects for window-level
  transitions (open / close / switch).
- QML-side shader effects drive overlay dissolves via Phase-4 QML
  types and Phase-3 AnimatedValue.

**Success criteria:**
- All 7 shader effects from PR #291 shipping as data files under
  `data/animations/`.
- Users can drop third-party shader packs into
  `~/.local/share/plasmazones/animations/` and pick them in Settings
  without a daemon restart.
- Zero daemon-side shader code outside the QtQuick item bridge.

**Estimated scope:** ~1500 LOC library (shader runtime + registry +
data-loader), ~300 LOC of QML migration, shader asset files preserved
from PR #291, 12+ new unit tests plus visual-smoke tests.

### Parallel housekeeping (not phased — can run alongside any phase)

- **`layoutsourcefactory.{h,cpp}` → `phosphor-layout-api`.** Removes
  three duplicated wirings (daemon, settings, editor) of the
  composite `ZonesLayoutSource + AutotileLayoutSource`
  assembly. 65 LOC move + lifetime-order struct that every consumer
  needs identically.
- **`dbus_helpers.h` logging-category ownership.** Currently depends
  on `lcCompositorCommon` in `compositor-common/logging.h`; the
  whole D-Bus triple (`dbus_types`, `dbus_constants`, `dbus_helpers`)
  can't extract until this is resolved. Three options: move the
  category into a target `phosphor-ipc`, pass the category in as a
  template parameter, or drop logging from `dbus_helpers` entirely.
- **Delete `debounced_action.h`.** No live consumers in PlasmaZones;
  it's dead weight that confuses the compositor-common decomposition
  audit. Park with a git note if there's a specific future use case
  in mind, otherwise remove.
- **Fix `overlayservice/settings.cpp:122` `ILayoutManager*` cast.**
  Extend `ILayoutManager` with signal declarations so the direct
  cast can go; unblocks `phosphor-overlay` extraction (Tier-2 per
  the library-extraction-survey).

---

## Testing Strategy

**Unit tests:** every curve, every public method, every virtual.
`libs/phosphor-animation/tests/` has 9 test executables covering
Easing, Spring, Curve base, CurveRegistry, Profile, ProfileTree,
WindowMotion, AnimationMath, and StaggerTimer. Each runs under
`QT_QPA_PLATFORM=offscreen` via the `phosphoranimation` ctest label.
No KWin, no Wayland, no running compositor required.

**Round-trip tests:** every curve serializes to a string, parses back,
and compares equal. Every profile / profile-tree round-trips through
JSON. This is the canonical safety net for config-format changes.

**Physics invariants:** Spring tests verify convergence (step × N
frames → within epsilon of target), retarget velocity preservation
(no stall on target change), and that pathological parameters
(`omega` near zero, `zeta` at boundaries) produce finite settle times.

**Timing tests:** StaggerTimer tests exercise both the synchronous
and `QTimer::singleShot` paths, plus the parent-destroyed
cancellation contract.

**Integration coverage:** the existing `test_compositor_common` still
passes unchanged after the shim removal — proof that every non-
animation compositor-common consumer is unaffected by the extraction.
Full PlasmaZones suite (116 tests) runs green after every phase.

---

## Open Questions

1. **~~Should `AnimationConfig` be retired in favor of `Profile`?~~**
   Resolved in the scaffold PR — `AnimationConfig` was a parallel-shape
   shim with no production benefit, so it was removed before merge.
   `createSnapMotion` now takes individual params; Phase 2 will migrate
   `WindowAnimator` to take a `Profile` and hold `shared_ptr<const Curve>`
   inside `WindowMotion`.

2. **Thread-safety posture beyond GUI thread.** Current contract is
   "all methods are GUI-thread-only." `CurveRegistry` has a mutex
   because registrations can happen from plugin loaders on arbitrary
   threads. Should `ProfileTree` get the same treatment for config-
   reload paths that might run on a worker thread? Current answer:
   no — ProfileTree is value-type, copied across threads rather than
   shared. Revisit if a use case appears.

3. **~~Spring physics for `WindowMotion` — is `std::shared_ptr<const
   Curve>` the right upgrade, or should WindowMotion carry a
   `CurveState` directly?~~** Resolved in Phase 2. `WindowMotion`
   carries **`std::shared_ptr<const Curve>` only** — no `CurveState`
   field. Rationale:

   - **Motion holds the curve definition by reference.** Multiple
     motions for different windows share one `shared_ptr<const Curve>`
     (immutable after construction per Curve's thread-safety contract).
     No copy-on-write, no duplication.
   - **Progression is parametric, not stateful.** `updateProgress`
     computes `t = elapsed / duration`, calls `curve->evaluate(t)`,
     and caches the scalar result in `cachedProgress`. This path
     uses the `Curve::evaluate(t)` API — the stateful `step()` API is
     intentionally not wired into `WindowMotion`.
   - **Why no `CurveState` in Phase 2.** A `CurveState` field only
     earns its weight once two things land together: (a) a clock that
     supplies `dt` (Phase 3's `IMotionClock`) and (b) a retarget policy
     surface that lets callers opt into velocity preservation (Phase
     3's `RetargetPolicy` enum on `AnimatedValue<T>`). Without both,
     a lone `CurveState` on `WindowMotion` would be a half-built
     physics engine — it would need the caller to compute `dt` from
     `presentTime`, would carry velocity through frames without a way
     to consume it on retarget, and would duplicate state that Phase 3
     puts in its natural home.
   - **Retarget semantics today.** When the consumer issues a new
     `startAnimation(handle, …)` for a handle that is already
     animating, it reads `currentVisualPosition` / `currentVisualSize`
     and passes those as the new start state. That is equivalent to
     `RetargetPolicy::ResetVelocity` in the Phase 3 design — the
     Spring's analytical `evaluate(t)` restarts from `t=0` at the
     captured visual position. A user dragging a window through
     multiple zones mid-snap sees smooth position continuity but not
     physical velocity continuity. The Phase 3 `AnimatedValue<T>` path
     — built on `curve->step(dt, state, target)` — is where
     velocity-preserving retarget lands.
   - **Terminal-frame clamp.** A parametric Spring's `evaluate(1)`
     returns a value within its settle band (e.g. 0.98 for underdamped
     at 2% settle), not exactly 1.0. `WindowMotion::updateProgress`
     clamps `cachedProgress` to `1.0` when `elapsed >= duration` so
     the final paint lands on target regardless of the curve's
     endpoint arithmetic.

4. **Plugin curve discovery.** Today `registerFactory` must be called
   imperatively. Phase 4/5 could add a `data/curves/*.json` discovery
   layer so third-party curve packs install as data files. Open; not
   urgent.

5. **Shader profile schema.** Phase 6 will need a per-event shader
   selection layered on top of `Profile`. Option A: extend `Profile`
   with an optional `shaderEffectId: QString`. Option B: separate
   `ShaderProfile` sibling with its own tree. Leaning A for
   simplicity, but it couples core `Profile` to the shader concept
   slightly. Decision deferred to Phase 6.

---

## References

- `docs/library-extraction-survey.md` — the strategic survey that
  scoped phosphor-animation and the other tier-1 extractions.
- `docs/wayfire-plugin-plan.md` — cross-reference for the compositor-
  plugin SDK ambition.
- `docs/phosphor-identity-api-design.md` — template for this doc's
  structure + dependency-graph convention.
- niri animation config — inspiration for the spring parameterization
  and hierarchical-event profile concept.
- Hyprland animation configs — inspiration for named-curve registry
  extensibility.
- Framer Motion / SwiftUI spring API — inspiration for the
  `(omega, zeta)` parameterization choice.
- PR #291 (closed, superseded) — reference for the shader-backed
  animation ambitions that Phase 6 delivers against the refactored
  v3 architecture.
