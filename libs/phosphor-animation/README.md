<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-animation

> Motion runtime (`AnimatedValue<T>` driven by curves, springs, and
> easings) and shader-transition runtime (per-event GPU effect
> selection), plus the JSON-backed `Profile` trees that configure both.

## Responsibility

Per-output clock, typed `AnimatedValue` interpolating toward a target,
polymorphic curves, and event-keyed profiles that map names like
`window.open` and `editor.snapIn` to motion configs.

The library ships two parallel runtimes:

- **Motion** (`PhosphorAnimation`) covers `AnimatedValue<T>`, polymorphic
  `Curve`s, `IMotionClock`, retarget / snap policy, stagger, and the
  `Profile` / `ProfileTree` / `PhosphorProfileRegistry` system.
- **Shader transitions** (`PhosphorAnimationShaders`), where
  `AnimationShaderRegistry` discovers transition shader packs from
  search paths. The parallel `ShaderProfile` / `ShaderProfileTree` maps
  the same event names to an effect plus parameter values.

Both profile trees use `std::optional` fields so a leaf can inherit
(`nullopt`) or override (engaged) anything its parent declares. They
share the dot-path event namespace but resolve through separate trees,
so a user can change the curve without touching the visual effect.

`SurfaceAnimator` implements
[`phosphor-layer`](../phosphor-layer/README.md)'s `ISurfaceAnimator`. For
a given event, it asks both registries what to play.

User-edited curves and profiles hot-reload from the user data dir
through [`phosphor-fsloader`](../phosphor-fsloader/README.md).

## Key types

### Motion runtime

| Type | Purpose |
|------|---------|
| `PhosphorAnimation::AnimatedValue<T>`         | Typed in-flight animation (rect, point, color, scalar). Uses a pull model where the consumer reads `value()` during paint. |
| `PhosphorAnimation::MotionSpec<T>`            | Runtime call-site bundle of profile, clock, retarget policy, and callbacks. |
| `PhosphorAnimation::IMotionClock`             | Pull-model clock contract, one per output for mixed refresh rates |
| `PhosphorAnimation::QtQuickClock`             | `QQuickWindow`-bound `IMotionClock` (gated on `PHOSPHOR_ANIMATION_QUICK=ON`) |
| `PhosphorAnimation::Curve`                    | Polymorphic base. Every curve family implements `step()` advancing a `CurveState` |
| `PhosphorAnimation::Easing`                   | Cubic-Bézier curve family (ease-out, ease-in-out, etc.) |
| `PhosphorAnimation::Spring`                   | Critically-damped spring with configurable tension and friction |
| `PhosphorAnimation::CurveRegistry`            | Name-to-curve factory that lets profiles reference curves by string |
| `PhosphorAnimation::Profile`                  | Serialisable bundle of curve + duration + stagger. `optional` fields support inherit/override |
| `PhosphorAnimation::ProfileTree`              | Hierarchical profile lookup with inheritance (`window.open` inherits from `window`) |
| `PhosphorAnimation::PhosphorProfileRegistry`  | Process-wide registry that hot-reloads profiles and emits live updates |
| `PhosphorAnimation::ProfileLoader`            | Sink for [`phosphor-fsloader`](../phosphor-fsloader/README.md) |
| `PhosphorAnimation::CurveLoader`              | Sink for [`phosphor-fsloader`](../phosphor-fsloader/README.md) |
| `PhosphorAnimation::RetargetPolicy`           | Mid-flight retarget behaviour: PreserveVelocity / ResetVelocity / PreservePosition |
| `PhosphorAnimation::SnapPolicy`               | Free helpers deciding whether a transition merits an animation |
| `PhosphorAnimation::StaggerTimer`             | Schedules animation starts across a group of windows |
| `PhosphorAnimationLayer::SurfaceAnimator`     | `ISurfaceAnimator` impl that wires both runtimes into [`phosphor-layer`](../phosphor-layer/README.md) |

### Shader-transition runtime

| Type | Purpose |
|------|---------|
| `PhosphorAnimationShaders::AnimationShaderEffect`   | Metadata for one transition (id, shader paths, parameter declarations) |
| `PhosphorAnimationShaders::AnimationShaderRegistry` | Discovers transition shader packs from search paths. Uses the metadata-pack scan strategy from [`phosphor-fsloader`](../phosphor-fsloader/README.md) |
| `PhosphorAnimationShaders::ShaderProfile`           | Per-event shader effect selection + parameter values |
| `PhosphorAnimationShaders::ShaderProfileTree`       | Hierarchical lookup matching `ProfileTree`'s shape |

## Typical use

Animate a window rect with a profile:

```cpp
#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/QtQuickClock.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/ProfileLoader.h>

using namespace PhosphorAnimation;

QtQuickClock clock(window);

// CurveLoader populates the curve registry first, then ProfileLoader
// scans profile files into the profile registry.
PhosphorProfileRegistry registry;
ProfileLoader loader(registry, curveRegistry);
loader.loadFromDirectory(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));

AnimatedValue<QRectF> geometry(initialRect);

MotionSpec<QRectF> spec{
    .profile        = registry.resolveWithInheritance(QStringLiteral("editor.snapIn")),
    .clock          = &clock,
    .retargetPolicy = RetargetPolicy::PreserveVelocity,
};
geometry.start(initialRect, targetRect, spec);

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

ShaderProfile sp = shaderProfileTree.resolve(QStringLiteral("window.open"));
QString effectId    = sp.effectId.value_or(QStringLiteral("dissolve"));
QVariantMap params  = sp.parameters.value_or(QVariantMap{});
```

## Design notes

- **Pull-model clock.** `AnimatedValue<T>::value()` reads `IMotionClock::now()`
  inside the consumer's paint cycle. No timers, no per-frame Qt signals.
  One clock per output keeps mixed refresh rates honest.
- **Polymorphic step contract.** Every `Curve` returns enough state for
  the consumer to act on (current value, velocity, settled flag). The
  `AnimatedValue` doesn't know or care whether the curve is a spring,
  an ease, or a user-defined Bézier.
- **Profile and ShaderProfile share an event namespace, not a tree.**
  `window.open` selects a motion profile and a transition effect, but
  through two independently-resolved trees, so a user can change the
  curve without touching the visual effect (or vice versa).
- **`optional` means inherit.** `ProfileTree` and `ShaderProfileTree`
  both treat `nullopt` fields as "look at the parent." Engaged fields
  win. This makes overrides surgical. Set one parameter and keep the rest
  of the bundle.
- **`PHOSPHOR_ANIMATION_QUICK` gates Qt Quick deps.** Headless tools and
  the KWin compositor adapter don't link `Qt6::Quick` because they don't
  build `QtQuickClock`. Core motion has only `QtCore` + `QtGui`.

## Dependencies

- `QtCore`, `QtGui`, `QtQuick`, `QtQuickPrivate`, `QtQml`
- [`phosphor-registry`](../phosphor-registry/README.md) — `Registry<T>` storage + `MetadataPackLoader<T>` backing `AnimationShaderRegistry` and `CurveRegistry`
- [`phosphor-fsloader`](../phosphor-fsloader/README.md) — directory loaders for curves, profiles, shader packs
- [`phosphor-shaders`](../phosphor-shaders/README.md) — header-only consumption of `CustomParamsKey.h`
- [`phosphor-layer`](../phosphor-layer/README.md) — `ISurfaceAnimator` (implemented by `SurfaceAnimator`)
- [`phosphor-rendering`](../phosphor-rendering/README.md)

## See also

- [`phosphor-fsloader`](../phosphor-fsloader/README.md) — its `MetadataPackScanStrategy` drives the `AnimationShaderRegistry`'s pack discovery (wrapped by [`phosphor-registry`](../phosphor-registry/README.md)'s `MetadataPackLoader<T>` into a `Registry<T>`). Profile and curve files come through its `DirectoryLoader`.
- [`phosphor-rendering`](../phosphor-rendering/README.md) — host items that consume the chosen effect's compiled shader.
- [`phosphor-shaders`](../phosphor-shaders/README.md) — overlay shader registry. This lib's animation registry uses the same metadata-pack scan strategy.
