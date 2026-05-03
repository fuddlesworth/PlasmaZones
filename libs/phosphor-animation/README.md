<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-animation

> Two cooperating runtimes in one library: a **motion runtime** that
> drives `AnimatedValue<T>` through curves, springs, and easings; and a
> **shader-transition runtime** that picks and parameterises the GPU
> effect played at each animation event. Plus the JSON-backed `Profile`
> trees that round-trip both through settings, D-Bus, and QML.

## Responsibility

Window snap-in, drag ghost, zone flash, and ambient shader-time updates
all want the same behaviour: a per-output clock, a typed `AnimatedValue`
that interpolates toward a target, a curve family that decides *how*
(ease, spring, custom Bézier), and an event-keyed profile that decides
*how strong / how long / which curve* without recompiling.

`phosphor-animation` ships two parallel runtimes:

- **Motion** (namespace `PhosphorAnimation`) — the `AnimatedValue<T>`
  family, polymorphic `Curve`s, the `IMotionClock` clock contract,
  retarget policy, snap policy, stagger, and the `Profile` /
  `ProfileTree` / `PhosphorProfileRegistry` system that maps event
  names like `window.open` and `zone.snapIn` to motion configs.
- **Shader transitions** (namespace `PhosphorAnimationShaders`) — the
  `AnimationShaderRegistry` that discovers transition shader packs from
  search paths, parameter metadata for a picker UI, and the parallel
  `ShaderProfile` / `ShaderProfileTree` that maps the same event names
  to a chosen effect plus its parameter values.

Both profile trees use `std::optional` fields so a leaf can inherit
(`nullopt`) or override (engaged) anything its parent declares. They share
the dot-path event namespace but resolve through separate trees so the
two concerns evolve independently.

`SurfaceAnimator` is the one place the two halves meet: it implements
[`phosphor-layer`](../phosphor-layer/README.md)'s `ISurfaceAnimator` and,
for a given event, asks both registries what to play.

User-edited curves and profiles hot-reload from the user data dir
through [`phosphor-fsloader`](../phosphor-fsloader/README.md) without a
daemon restart.

## Key types — motion runtime

| Type | Purpose |
|------|---------|
| `PhosphorAnimation::AnimatedValue<T>`         | Typed in-flight animation (rect, point, color, scalar). Pull-model: consumer reads `value()` during paint. |
| `PhosphorAnimation::MotionSpec<T>`            | Runtime call-site bundle: profile, clock, retarget policy, callbacks. |
| `PhosphorAnimation::IMotionClock`             | Pull-model clock contract; one per output for mixed refresh rates |
| `PhosphorAnimation::QtQuickClock`             | `QQuickWindow`-bound `IMotionClock` (gated on `PHOSPHOR_ANIMATION_QUICK=ON`) |
| `PhosphorAnimation::Curve`                    | Polymorphic base; every curve family implements `step()` returning a `WindowMotion` |
| `PhosphorAnimation::Easing`                   | Cubic-Bézier curve family (ease-out, ease-in-out, etc.) |
| `PhosphorAnimation::Spring`                   | Critically-damped spring with configurable tension and friction |
| `PhosphorAnimation::CurveRegistry`            | Name-to-curve factory; lets profiles reference curves by string |
| `PhosphorAnimation::Profile`                  | Serialisable bundle of curve + duration + stagger; `optional` fields support inherit/override |
| `PhosphorAnimation::ProfileTree`              | Hierarchical profile lookup with inheritance (`window.open` inherits from `window`) |
| `PhosphorAnimation::PhosphorProfileRegistry`  | Process-wide registry that hot-reloads profiles and emits live updates |
| `PhosphorAnimation::ProfileLoader`            | Sink for [`phosphor-fsloader`](../phosphor-fsloader/README.md) |
| `PhosphorAnimation::CurveLoader`              | Sink for [`phosphor-fsloader`](../phosphor-fsloader/README.md) |
| `PhosphorAnimation::RetargetPolicy`           | Mid-flight retarget behaviour: PreserveVelocity / ResetVelocity / PreservePosition |
| `PhosphorAnimation::SnapPolicy`               | Free helpers deciding whether a transition merits an animation |
| `PhosphorAnimation::StaggerTimer`             | Schedules animation starts across a group of windows |
| `PhosphorAnimation::SurfaceAnimator`          | `ISurfaceAnimator` impl that wires both runtimes into [`phosphor-layer`](../phosphor-layer/README.md) |

## Key types — shader-transition runtime

| Type | Purpose |
|------|---------|
| `PhosphorAnimationShaders::AnimationShaderEffect`   | Metadata for one transition (id, shader paths, parameter declarations) |
| `PhosphorAnimationShaders::AnimationShaderRegistry` | Discovers transition shader packs from search paths; metadata-pack scan strategy from [`phosphor-fsloader`](../phosphor-fsloader/README.md) |
| `PhosphorAnimationShaders::ShaderProfile`           | Per-event shader effect selection + parameter values |
| `PhosphorAnimationShaders::ShaderProfileTree`       | Hierarchical lookup matching `ProfileTree`'s shape |

## Typical use

Animate a window rect with a profile:

```cpp
#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/QtQuickClock.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>

using namespace PhosphorAnimation;

QtQuickClock clock(window);
PhosphorProfileRegistry registry;
registry.addSearchPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
registry.refresh();

AnimatedValue<QRectF> geometry(initialRect);

MotionSpec<QRectF> spec{
    .profile        = registry.profileFor(QStringLiteral("zone.snapIn")),
    .clock          = &clock,
    .retargetPolicy = RetargetPolicy::PreserveVelocity,
};
geometry.start(targetRect, spec);

// In paint:
QRectF current = geometry.value();
```

Pick a transition via the shader runtime:

```cpp
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

using namespace PhosphorAnimationShaders;

AnimationShaderRegistry shaders;
shaders.addSearchPath(systemDir);
shaders.addSearchPath(userDir);
shaders.refresh();

ShaderProfile sp = shaderProfileTree.profileFor(QStringLiteral("window.open"));
QString effectId    = sp.effectId.value_or(QStringLiteral("dissolve"));
QVariantMap params  = sp.params.value_or(QVariantMap{});
```

## Design notes

- **Pull-model clock.** `AnimatedValue<T>::value()` reads `IMotionClock::now()`
  inside the consumer's paint cycle. No timers; no per-frame Qt signals.
  One clock per output keeps mixed refresh rates honest.
- **Polymorphic step contract.** Every `Curve` returns enough state for
  the consumer to act on (current value, velocity, settled flag). The
  `AnimatedValue` doesn't know or care whether the curve is a spring,
  an ease, or a user-defined Bézier.
- **Profile and ShaderProfile share an event namespace, not a tree.**
  `window.open` selects a motion profile *and* a transition effect, but
  through two independently-resolved trees, so a user can change the
  curve without touching the visual effect (or vice versa).
- **`optional` means inherit.** `ProfileTree` and `ShaderProfileTree`
  both treat `nullopt` fields as "look at the parent." Engaged fields
  win. This makes overrides surgical — set one parameter, keep the rest
  of the bundle.
- **`PHOSPHOR_ANIMATION_QUICK` gates Qt Quick deps.** Headless tools and
  the KWin compositor adapter don't link `Qt6::Quick` because they don't
  build `QtQuickClock`. Core motion has only `QtCore` + `QtGui`.

## Dependencies

- `QtCore`, `QtGui`, `QtQml` (always)
- `Qt6::Quick` only when `PHOSPHOR_ANIMATION_QUICK=ON`
- [`phosphor-fsloader`](../phosphor-fsloader/README.md) — directory loaders for curves, profiles, shader packs
- [`phosphor-layer`](../phosphor-layer/README.md) — `ISurfaceAnimator` (implemented by `SurfaceAnimator`)

## See also

- [`phosphor-fsloader`](../phosphor-fsloader/README.md) — `MetadataPackRegistryBase` powers the `AnimationShaderRegistry`; profile / curve files come through its `DirectoryLoader`.
- [`phosphor-rendering`](../phosphor-rendering/README.md) — host items that consume the chosen effect's compiled shader.
- [`phosphor-shaders`](../phosphor-shaders/README.md) — overlay shader registry; this lib's animation registry uses the same metadata-pack scan strategy.
