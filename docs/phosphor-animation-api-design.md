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
   any window-system. `WindowMotion` (Phase 2) / `AnimatedValue<QRectF>`
   (Phase 3+) describe geometry transitions in pure `QPointF` /
   `QSizeF` / `QRect` / `QRectF` terms. The KWin-specific paint glue
   lives in `kwin-effect/windowanimator.cpp`; future compositor plugins
   ship their own `IMotionClock` adapters against the same state model.

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

### `WindowMotion.h` — (🗑 deleted in Phase 3 sub-commit 3)

> **Status:** removed. Fields became internal state of
> `AnimatedValue<QRectF>`; the snap-animation path flows through
> `SnapPolicy::createSnapSpec` → `AnimatedValue<QRectF>::start()`.
> The description below documents the Phase-2 shipped API for
> historical reference only; the header and tests no longer exist.


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

### `AnimationMath.h` (🗑 deleted in Phase 3 sub-commit 3)

> **Status:** removed. `createSnapMotion` was replaced by
> `SnapPolicy::createSnapSpec` (returns `std::optional<MotionSpec<QRectF>>`
> instead of `std::optional<WindowMotion>`); `repaintBounds` was
> absorbed into `AnimatedValue<QRectF>::bounds()` and the
> `Curve::overshoots()` polymorphism. The description below documents
> the Phase-2 shipped API for historical reference only; the header
> and tests no longer exist.


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

### Phase 3 — Unified motion runtime (IMotionClock + AnimatedValue<T>)

**Status:** architecture ratified 2026-04-18; implementation pending
on `feat/phosphor-animation-phase3`.

#### Scope shift from the original Phase 3 draft

The original draft scoped Phase 3 as a clock abstraction plus a generic
`AnimatedValue<T>` *alongside* the existing `WindowMotion`. Reviewing
with niri / Hyprland / Quickshell / Wayfire-tier capability as the
target and v3's no-backwards-compat freedom:

**Decision — full unification.** `AnimatedValue<T>` *replaces*
`WindowMotion`. `AnimationController<Handle>` is rewritten on top of
`QHash<Handle, AnimatedValue<QRectF>>`. Two parallel progression
engines is exactly the duplication Phase 2 was designed to eliminate;
preserving `WindowMotion` just because Phase 2 shipped it would be the
wrong trade for v3.

#### Architecture decisions

**A. `IMotionClock` — pull model, per-output scope, outgoing `requestFrame()`.**

```cpp
class PHOSPHORANIMATION_EXPORT IMotionClock {
public:
    virtual ~IMotionClock() = default;
    virtual std::chrono::nanoseconds now() const = 0;
    virtual qreal refreshRate() const = 0;   // Hz
    virtual void requestFrame() = 0;         // schedule next tick
};
```

Pull model: compositor paint loops and `QQuickWindow::beforeRendering`
are both paint-driven; a push-model clock would duplicate the
subscription machinery the paint loop already owns. Outgoing
`requestFrame()` replaces the compositor-specific `addRepaint` /
QQuickWindow `update()` patterns with one portable call — it's the
*only* outgoing edge on the clock. Per-output scope is non-negotiable
for multi-monitor phase-locking; niri and Hyprland both multiplex
per-monitor refresh rates (60 Hz + 144 Hz mixed), and a process-wide
clock produces visible beating / double-stepping.

Concrete impls:
- `CompositorClock` — per-`KWin::Output`, driven by
  `prePaintScreen(presentTime)`; `requestFrame()` forwards to
  `KWin::effects->addRepaint()`.
- `QtQuickClock` — per-`QQuickWindow`, driven by `beforeRendering`;
  `requestFrame()` forwards to `QQuickWindow::update()`.

**B. `AnimatedValue<T>` replaces `WindowMotion`.** The load-bearing
decision. The snap-animation path becomes `AnimatedValue<QRectF>`;
non-geometry animations (opacity, color, transform, scalar shader
uniforms) use other specializations of the same template. Snap-
specific policy (`minDistance`, `hasScaleChange`) moves into a
`SnapPolicy::createSnapSpec` helper that returns a
`std::optional<MotionSpec<QRectF>>`. The four Phase-2 adapter hooks
(`onAnimationStarted`, `onAnimationComplete`, `onRepaintNeeded`,
`isHandleValid`, `expandedPadding`) stay — they're compositor-
integration surface, not progression logic.

**C. `RetargetPolicy` honored per-curve-capability, not per-signature.**
Stateless Easing honors `PreservePosition` (set
`state.startValue = currentValue; state.time = 0`) and `ResetVelocity`
(reset to `from`) as genuinely distinct behaviors. `PreserveVelocity`
on stateless curves degrades to `PreservePosition` with a debug log —
there is no physical velocity to preserve. Stateful Spring honors all
three meaningfully. A compile-time `requires` constraint would
fragment the API and block polymorphic retarget loops where the
concrete curve type isn't known to the caller; capability-gating at
runtime is the right choice.

**D. Clock injection at `start()` time — no defaults, no registry.**
Per-output clocks make defaults fundamentally wrong (a window moving
between monitors must retarget its clock). No process-wide default;
no scope-local registry. `start(from, to, MotionSpec)` carries the
clock inside `MotionSpec`. QML's `Behavior { PhosphorMotion { ... } }`
(Phase 4) pulls `item.window()->motionClock()` automatically so call
sites stay clean without a hidden lookup.

**E. `bounds()` is a geometric specialization only.** For geometric
`T` (`QPointF`, `QSizeF`, `QRectF`) the bounds *are* the damage region
(start ∪ target + overshoot sample). For scalar `T` (`qreal`, `int`),
`sweptRange() → std::pair<T, T>` covers the motion range. For
`QColor` / `QTransform` / opaque `T`, damage is owned by the *item
the value drives* (opacity of an item invalidates that item's rect,
not the animation's "bounds") — so the animation exposes neither
`bounds()` nor `sweptRange()`, and the consumer drives damage via the
`onValueChanged` callback (see J).

**F. `QColor` — linear-space RGB default, `OkLab` opt-in via template
tag.** sRGB component-wise lerp of complementary colors produces grey
midpoints — visibly wrong at the tier we're targeting. Compositor
shaders already work in linear space for blending; matching that is
consistent. Convert sRGB→linear once at `start()` (cached), interpolate
linear, convert back in `value()`. `AnimatedValue<QColor, OkLab>` is
the perceptually-uniform opt-in for color pickers and gradient strips
where perceptual uniformity matters more than render-correctness.

**G. `QTransform` — polar decomposition, never component-wise.**
Component-wise lerp of affine matrices shears during rotation.
Implementation: decompose into translate + rotate + scale + shear;
lerp translate/scale linearly, rotate via shortest-arc slerp, shear
linearly (rare enough to accept the degradation). Implementation
detail — user sees "transforms interpolate correctly through rotation".

**H. `Profile` stays serializable config. `MotionSpec<T>` is the
runtime call-site bundle.** `Profile = { duration, curve, sequence,
staggerInterval, presetName }` is the user-facing config surface
that round-trips through JSON / D-Bus / settings UI. Runtime concerns
(clock reference, retarget policy, per-animation callbacks) have no
business in a serialized profile.

```cpp
template<typename T>
struct MotionSpec {
    Profile profile;
    IMotionClock* clock = nullptr;                 // required
    RetargetPolicy retargetPolicy = RetargetPolicy::PreserveVelocity;
    std::function<void(const T&)> onValueChanged;  // damage hook (J)
    std::function<void()> onComplete;
};
```

**I. Composite animations are first-class; `AnimatedValue<T>` is the
atom.** niri / Hyprland configure "window open" as opacity + scale +
position — three animations on one surface. Phase 3 ships atoms with
composite-ready API (no atomic API that breaks when wrapped by a
sequencer); `CompositeAnimation` with parallel / sequence / stagger
operators lands in Phase 3.5 or Phase 4 alongside QML bindings. The
atom must not leak assumptions that it's top-level (e.g. an atom's
`onComplete` firing directly doesn't preclude a sequencer intercepting
it).

**J. Damage / invalidation — callback-driven, not polled.**
`MotionSpec::onValueChanged = std::function<void(const T&)>;`.
Compositor adapter installs `effects->addRepaint(bounds)`; QML
`Behavior` installs `item->polishAndUpdate()`. Polling (`value()` is
always cheap) is opt-in, but the default damage path is push from the
animation to its driver. Keeps shader / uniform / custom consumers
from having to invent their own dirty-tracking.

**K. Config reload — immutable curves, atomic profile swap, no
in-flight migration.** `shared_ptr<const Curve>` held per-animation
survives a registry swap (the curve the animation started with is the
curve it completes on). `ProfileRegistry::update(path, newProfile)`
atomically swaps the registry entry; new `start()` calls read the new
profile, in-flight animations do not migrate. "Live re-tune during
animation" is a Phase 4 feature if a use case appears
(`AnimatedValue::adopt(newProfile, policy)`).

#### Public API shape

```cpp
// RetargetPolicy.h
enum class RetargetPolicy {
    PreserveVelocity,   // default — stateful curves honor; stateless → PreservePosition + debug log
    ResetVelocity,
    PreservePosition,
};
```

```cpp
// AnimatedValue.h — base template + geometric / scalar / color specialisations
template<typename T>
class AnimatedValue {
public:
    bool start(T from, T to, MotionSpec<T> spec);
    bool retarget(T newTo, RetargetPolicy policy);
    void cancel();
    void finish();

    T value() const;
    T velocity() const;           // meaningful for stateful curves
    bool isAnimating() const;
    bool isComplete() const;

    void advance();               // pulls dt from clock, calls step()/evaluate()
};

// Geometric specialisations expose bounds() → QRectF (with overshoot)
template<> QRectF AnimatedValue<QPointF>::bounds() const;
template<> QRectF AnimatedValue<QSizeF>::bounds() const;
template<> QRectF AnimatedValue<QRectF>::bounds() const;

// Scalar specialisation exposes sweptRange()
template<> std::pair<qreal, qreal> AnimatedValue<qreal>::sweptRange() const;

// QColor: linear-space default, OkLab opt-in via template tag
enum class ColorInterpolation { Linear, OkLab };
template<ColorInterpolation Space = ColorInterpolation::Linear>
class AnimatedValue<QColor, Space>;   // partial specialisation

// QTransform: polar-decomposed lerp
template<> class AnimatedValue<QTransform>;
```

```cpp
// SnapPolicy.h — extracts the Phase-2 createSnapMotion gate
namespace SnapPolicy {
    struct SnapParams {
        qreal duration;
        std::shared_ptr<const Curve> curve;
        int minDistance;
    };

    std::optional<MotionSpec<QRectF>>
    createSnapSpec(const QRectF& oldFrame, const QRectF& newFrame,
                   const SnapParams& params, IMotionClock* clock);
}
```

#### AnimationController rewrite

```cpp
template<typename Handle>
class AnimationController {
public:
    void setClock(IMotionClock* clock);
    void setProfile(const Profile& profile);
    void setMinDistance(int pixels);        // clamped [0, 10000]
    void setEnabled(bool enabled);

    bool startAnimation(Handle handle, const QRectF& oldFrame, const QRectF& newFrame);
    bool retarget(Handle handle, const QRectF& newFrame, RetargetPolicy policy);
    void removeAnimation(Handle handle);
    void clear();

    bool hasAnimation(Handle handle) const;
    bool hasActiveAnimations() const;
    bool isAnimatingToTarget(Handle handle, const QRectF& target) const;
    QRectF currentValue(Handle handle, const QRectF& fallback) const;
    QRectF animationBounds(Handle handle) const;  // delegates to AnimatedValue<QRectF>::bounds()

    void advanceAnimations();                // pulls dt from clock
    void scheduleRepaints() const;

protected:
    // Four adapter hooks preserved from Phase 2:
    virtual void onAnimationStarted(Handle, const AnimatedValue<QRectF>&) {}
    virtual void onAnimationComplete(Handle, const AnimatedValue<QRectF>&) {}
    virtual void onRepaintNeeded(Handle, const QRectF&) const {}
    virtual bool isHandleValid(Handle) const { return true; }
    virtual QMarginsF expandedPadding(Handle, const AnimatedValue<QRectF>&) const { return {}; }

private:
    QHash<Handle, AnimatedValue<QRectF>> m_animations;
    IMotionClock* m_clock = nullptr;
    Profile m_profile;
    int m_minDistance = 0;
    bool m_enabled = true;
};
```

Multi-output adapters hold one controller per output (each with its
output's clock) rather than juggling per-animation clocks inside a
single controller; the controller is intentionally single-clock for
cache-locality and to make the "which clock drives this animation"
question impossible to get wrong.

#### Retirements (Phase 3 deletes)

- `WindowMotion.h` — deleted. Its fields (`startPosition`, `startSize`,
  `targetGeometry`, `startTime`, `duration`, `curve`, `cachedProgress`)
  become internal state of `AnimatedValue<QRectF>`.
- `AnimationMath::createSnapMotion` — deleted. Replaced by
  `SnapPolicy::createSnapSpec`.
- `AnimationMath::repaintBounds` — deleted. Its logic moves inside
  `AnimatedValue<QRectF>::bounds()` and parameterizes on the internal
  curve via `Curve::overshoots()`.

#### Success criteria

- A QML-side animation (`AnimatedValue<qreal>` on opacity) and a
  compositor-side animation (`AnimatedValue<QRectF>` on window frame)
  driven by the same `Profile` with the same curve produce visually
  identical motion on a test harness.
- Spring retarget-mid-flight (A → B, midway `retarget(C,
  PreserveVelocity)`) — velocity preserved across the discontinuity,
  no stall, no visible jump.
- Multi-output: two displays at 60 Hz and 144 Hz animate
  simultaneously; each animation steps on its own output's clock
  without double-stepping or beating. Verified with a mock two-output
  test harness.
- `QColor` lerp midpoint for `Qt::red → Qt::green` is visibly green-
  biased in linear-space RGB (vs. the muddy grey that sRGB component-
  wise produces); opt-in OkLab produces a further-corrected perceptual
  midpoint.
- `QTransform` lerp from identity to `rotate(90°)` passes through
  `rotate(45°)` exactly, not a sheared intermediate.
- Config reload: `ProfileRegistry::update(path, newProfile)` while
  motions are in flight does not mutate those motions' progression
  (`shared_ptr<const Curve>` immutability invariant).

#### Tests (~30 new cases)

- `pa_test_imotionclock` — clock injection, per-output isolation,
  `requestFrame()` forwarding (mock driver).
- `pa_test_animatedvalue_scalar` — start / retarget / cancel / finish;
  all three `RetargetPolicy` values on stateful and stateless curves
  (6 retarget cases, plus capability-downgrade log assertion).
- `pa_test_animatedvalue_geometry` — `QPointF` / `QSizeF` / `QRectF`
  bounds() + overshoot sampling; swept-rect correctness.
- `pa_test_animatedvalue_color` — sRGB-vs-linear midpoint verification
  (the "complementary colors midway is not grey" regression), OkLab
  opt-in path.
- `pa_test_animatedvalue_transform` — polar decomposition correctness,
  rotation without shear, shortest-arc slerp endpoint handling.
- `pa_test_snappolicy` — ported from the removed `createSnapMotion`
  tests; `MotionSpec<QRectF>` return shape.
- `pa_test_animationcontroller` — existing Phase-2 cases rewritten
  over `AnimatedValue<QRectF>`; re-entrancy contracts preserved;
  two-controller multi-output test added.
- `pa_test_config_reload` — profile swap does not mutate in-flight
  motions; new `start()` after swap reads the new profile.

#### Migration plan (sub-commits on the Phase 3 branch)

1. **Clock abstraction.** Land `IMotionClock` + `CompositorClock`
   (KWin-backed) as new headers. No consumer changes yet;
   `AnimationController` / `WindowMotion` still ship unchanged. Unit
   tests for the interface shape.
2. **AnimatedValue core.** Land `MotionSpec<T>`, `RetargetPolicy`, and
   the `AnimatedValue<T>` template with `qreal` / `QPointF` / `QSizeF`
   / `QRectF` specializations. Still no consumer migration. Tests:
   scalar/geometry specs, retarget continuity on stateful and stateless
   curves.
3. **Controller rewrite + `WindowMotion` delete.** Rewrite
   `AnimationController<Handle>` on top of `AnimatedValue<QRectF>` +
   `CompositorClock`. Delete `WindowMotion`, delete
   `AnimationMath::createSnapMotion` + `repaintBounds`. Update the
   KWin adapter. Phase-2 tests rewritten; they must stay green.
4. **Color + transform specialisations.** Add `AnimatedValue<QColor>`
   (linear-space + OkLab) and `AnimatedValue<QTransform>` (polar
   decomposition). No consumer wiring yet — they exist for Phase 4
   QML consumers.
5. **QtQuickClock + optional CMake flag.** Add `QtQuickClock` impl.
   Gate `Qt6::Quick` behind an optional CMake flag
   (`PHOSPHOR_ANIMATION_QUICK=ON`, default OFF) so non-QML consumers
   don't pull Qt Quick transitively.

Each sub-commit is independently shippable + reviewable; full test
suite green after each.

#### Estimated scope

~1500 LOC library (original ~700 + `AnimatedValue<T>` specialization
set + `AnimationController` rewrite + `QtQuickClock`), ~30 new unit
tests, ~50 LOC KWin adapter migration.

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

**Phase 3 architecture pass (resolved 2026-04-18):** Eight implicit
architectural questions that the original Phase 3 draft did not
explicitly answer — clock shape (push vs pull, per-output vs
process-wide), `WindowMotion`-vs-`AnimatedValue` unification,
`RetargetPolicy` semantics on stateless curves, clock injection
model, `bounds()` for non-geometric `T`, `QColor` interpolation space,
`QTransform` decomposition approach, `Profile`-vs-`MotionSpec` split —
are all resolved in decisions A–K of the Phase 3 roadmap section
above. The load-bearing call: full unification.
`AnimatedValue<T>` replaces `WindowMotion`, and
`AnimationController<Handle>` is rewritten on top of
`QHash<Handle, AnimatedValue<QRectF>>` — v3's no-backwards-compat
freedom makes this the architecturally correct choice over preserving
two parallel progression engines.

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
