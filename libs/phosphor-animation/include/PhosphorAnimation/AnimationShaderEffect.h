// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

namespace PhosphorAnimationShaders {

/**
 * @brief Metadata for a single animation shader effect (transition).
 *
 * Distinct from `PhosphorRendering::ShaderEffect` which renders persistent
 * zone backgrounds. An AnimationShaderEffect describes a *transition* —
 * dissolve, slide, morph, etc. — applied between two visual states over
 * a finite duration.
 *
 * ## Identity
 *
 * Each effect is keyed by a stable `id` string (e.g. "dissolve", "slide",
 * "glitch"). Plugin-authored effects use reverse-domain or namespaced ids
 * ("myplugin.sparkle") to avoid collisions with built-in effects.
 *
 * ## Shader source
 *
 * Each effect carries a single `fragmentShaderPath` — the same GLSL
 * source is used on both the QtQuick and KWin compositor paint paths.
 * `PhosphorRendering::ShaderNodeRhi` handles texture binding and
 * coordinate conventions at the RHI level, so separate per-backend
 * shader variants are not needed.
 *
 * ## Parameters
 *
 * Each effect declares named parameters with type, default, min/max.
 * Per-event parameter overrides live on `ShaderProfile`, not here.
 */
struct PHOSPHORANIMATION_EXPORT AnimationShaderEffect
{
    /// Stable identifier — lookup key in the registry.
    QString id;

    /// Human-readable display name (localizable by the consumer).
    QString name;

    /// One-line description for settings UI tooltips.
    QString description;

    /// Author attribution.
    QString author;

    /// Semantic version of this effect pack.
    QString version;

    /// Category for settings-UI grouping (e.g. "Fade", "Geometric", "Glitch").
    QString category;

    /// Event-class tokens this effect is meaningful on. Each token is one
    /// of `PhosphorAnimation::ProfilePaths::EventClassAppearance`
    /// ("appearance" — open/close/minimize/focus + OSD/popup show/hide),
    /// `EventClassGeometry` ("geometry" — snap*/layoutSwitch/maximize),
    /// `EventClassDesktop` ("desktop" — the two-texture
    /// full-screen switch) or `EventClassMove` ("move" — the held
    /// interactive drag, driven by the move-physics inputs). EMPTY (the
    /// default) means "universal" — the effect applies to every
    /// single-surface event class, which is the right answer for the bulk
    /// of transitions (fade, glitch, dissolve …) that operate on a single
    /// surface and need no before/after geometry. The desktop and move
    /// classes are opt-in and never covered by "universal".
    ///
    /// A geometry-only effect like `window-morph` cross-fades an old rect
    /// into a new rect (`iFromRect → iToRect`); that pair only exists on
    /// geometry legs, so on an appearance event it would render nothing.
    /// Declaring `["geometry"]` lets the settings picker dim the effect on
    /// appearance rows with an explanatory tooltip instead of letting a
    /// user assign a silent no-op. See `shaderEffectAppliesToEventPath`.
    QStringList appliesTo;

    /// Path to the fragment shader (relative to the effect dir). The same
    /// source is used on both QtQuick and KWin paths — ShaderNodeRhi
    /// handles backend differences at the RHI level.
    QString fragmentShaderPath;

    /// Path to the vertex shader (relative to the effect dir). Empty = use
    /// the library's built-in fullscreen-quad vertex shader.
    QString vertexShaderPath;

    /// Resolved absolute directory containing this effect's assets.
    QString sourceDir;

    /// Whether this effect was loaded from a user-local directory.
    bool isUserEffect = false;

    /// Preview image path (relative to the effect dir). For settings UI.
    QString previewPath;

    // ── Multipass / advanced features (opt-in, matching overlay ShaderInfo) ──

    /// Buffer-pass shader paths (relative to effect dir). When non-empty
    /// and `isMultipass` is true, the daemon's ShaderEffect runs these as
    /// intermediate passes before the main fragment shader.
    QStringList bufferShaderPaths;

    /// Opt-in multipass mode. When true and `bufferShaderPaths` is
    /// non-empty, the daemon path runs those buffer passes before the
    /// main fragment. The kwin-effect compositor path is single-pass
    /// only; multipass effects degrade to single-pass there with a
    /// diagnostic log (see `AnimationShaderContract.h`).
    bool isMultipass = false;

    /// Bind the user's wallpaper as a sampler. Daemon-only — the
    /// compositor path has no wallpaper plumbing so the field is
    /// observed only by `SurfaceAnimator::attachShaderToAnchor`.
    bool useWallpaper = false;

    /// Enable per-pass feedback (last frame's buffer is sampleable as
    /// `iChannel<N>`). Requires `isMultipass`. Daemon-only.
    bool bufferFeedback = false;

    /// Render-target scale relative to the surface size. Clamped to
    /// `[0.125, 1.0]` at `fromJson` time. Daemon-only — the compositor
    /// path doesn't allocate auxiliary FBOs.
    qreal bufferScale = 1.0;

    /// Default wrap mode for all buffer samplers. Sibling of
    /// `bufferWraps` (per-buffer overrides). Empty = ShaderEffect
    /// default. Daemon-only.
    QString bufferWrap;

    /// Per-buffer wrap-mode overrides; index aligns with
    /// `bufferShaderPaths`. Daemon-only.
    QStringList bufferWraps;

    /// Default filter mode for all buffer samplers. Empty = ShaderEffect
    /// default. Daemon-only.
    QString bufferFilter;

    /// Per-buffer filter-mode overrides; index aligns with
    /// `bufferShaderPaths`. Daemon-only.
    QStringList bufferFilters;

    /// Allocate a depth buffer alongside the colour FBO so the shader
    /// can sample window depth. Daemon-only.
    bool useDepthBuffer = false;

    /// Audio-reactive opt-in (JSON key `audio`, matching the surface pack
    /// vocabulary). The pack samples the CAVA spectrum via
    /// `data/animations/shared/audio.glsl`. Unlike the overlay family, where
    /// audio is implicit/session-global, animation packs must declare it:
    /// the kwin-effect keys its CAVA run-gate on this flag (an assigned
    /// audio pack keeps the provider warm so a transition's first frame has
    /// a spectrum), and the daemon's SurfaceAnimator feeds the spectrum
    /// unconditionally. Helpers read 0 (render static) when the visualizer
    /// is off or cava is unavailable.
    bool useAudio = false;

    /// How wide the shader effect's render target is — relative to its
    /// anchor (default), or filling the surface scene root.
    ///
    ///   • `Anchor` (default): FBO covers the captured anchor 1:1. Used
    ///     by fragment-only effects (dissolve, glitch, …) whose math
    ///     stays inside `vTexCoord ∈ [0, 1]` over the anchor.
    ///
    ///   • `Surface`: FBO covers the anchor's enclosing surface scene
    ///     root (the wl_surface size on the daemon path). Used by
    ///     effects that need to render past the captured anchor
    ///     (fly-in translates the card across the surface, broken-glass
    ///     fires shards into the surrounding screen). Authors who pick
    ///     `Surface` typically ship a custom vertex shader that uses
    ///     `iAnchorPosInFbo / iAnchorSize / iResolution` to position
    ///     the card within the FBO; the fragment shader then either
    ///     reads `vTexCoord` directly (if the vert remaps the quad to
    ///     the anchor's region — fly-in) or calls `anchorRemap` to
    ///     convert surface-UV back to anchor-space (broken-glass,
    ///     morph).
    ///
    /// Mirrors the public `fboExtent` JSON grammar: `"anchor"` →
    /// `Anchor`, `"surface"` → `Surface`. No other forms are accepted.
    enum class FboExtentKind {
        Anchor,
        Surface,
    };
    FboExtentKind fboExtentKind = FboExtentKind::Anchor;

    /// Per-axis subdivision count for geometry shaders that deform the
    /// drawn quad in the vertex stage (e.g. the `flow` window-move
    /// effect, whose grid rows lag behind the leading edge so the
    /// window streams into its destination zone). 0 (the default) keeps
    /// the single output-spanning quad every other surface-extent
    /// shader uses. A value of N tells the kwin-effect's `apply()` to
    /// emit an N×N grid of quad cells over the window's destination rect
    /// so the vertex shader has interior vertices to displace. Only
    /// meaningful together with
    /// `fboExtent: "surface"`; ignored otherwise. JSON key
    /// `"geometryGrid"`, omitted from `toJson` when 0 to keep authored
    /// metadata terse, same idiom as `fboExtent` / `multipass`.
    int geometryGridSubdivisions = 0;

    /// Lower / upper bounds on `bufferScale` (multipass FBO downscale
    /// factor). 0.125 means a 1/8 downscale on each axis (1/64 area —
    /// the lowest cost-floor that still gives Shadertoy-style buffer
    /// effects something to work with). 1.0 means full-resolution
    /// FBOs (no downscale). Hosted here as the source-of-truth that
    /// `fromJson`'s clamp + the round-trip stability comment in
    /// `toJson` reference; a future runtime that consumes bufferScale
    /// from a non-JSON source can read these constants directly.
    static constexpr qreal kMinBufferScale = 0.125;
    static constexpr qreal kMaxBufferScale = 1.0;
    static_assert(kMinBufferScale > 0.0 && kMinBufferScale < kMaxBufferScale,
                  "kMinBufferScale must be positive and strictly less than kMaxBufferScale");

    /// Declared shader inputs beyond the standard set (iTime, iFrame, etc.).
    /// Each entry maps `parameterId → { type, default, min, max, ... }`.
    /// Field names mirror the regular shader pack format
    /// (PhosphorRendering::ShaderRegistry::ParameterInfo) so animation
    /// packs and overlay packs can share QML editor components.
    ///
    /// **C++ field name vs JSON key asymmetry**: the QVariant fields are
    /// suffixed `Value` because `default` is a C++ keyword in some
    /// contexts; the wire-format and QML-facing keys are the bare forms
    /// (`default`/`min`/`max`/`step`). See `toJson()` / `fromJson()` in
    /// `animationshadereffect.cpp` for the mapping, and
    /// `AnimationsPageController::parameterInfoToMap` for the QML side.
    struct ParameterInfo
    {
        QString id;
        QString name;
        QString type; ///< "float", "int", "bool", "color"
        QString description; ///< Optional one-line tooltip for the settings UI.
        QString group; ///< Optional accordion group name for the settings UI.
        QVariant defaultValue; ///< JSON/QML key: `default`.
        QVariant minValue; ///< JSON/QML key: `min`.
        QVariant maxValue; ///< JSON/QML key: `max`.
        QVariant stepValue; ///< Optional slider step; JSON/QML key: `step`. QML falls back to (max-min)/200.
    };
    QList<ParameterInfo> parameters;

    /// User texture slot. Each entry binds an asset file to one of the
    /// canonical samplers `uTexture1` / `uTexture2` / `uTexture3` (slots
    /// 0 / 1 / 2 here, which the runtimes map to texture-unit
    /// allocations 1 / 2 / 3 — slot 0 of the binding-7+ region is the
    /// surface itself / `uTexture0`, never user-declared).
    ///
    /// `path` is resolved relative to the effect's `sourceDir`. Loading
    /// failures are non-fatal — the effect still installs and the
    /// affected sampler reads transparent black; a `qCWarning` logs the
    /// missing file. `wrap` mirrors the daemon-side overlay shader
    /// vocabulary; the only accepted values are `"clamp"`, `"repeat"`,
    /// `"mirror"`, and the empty string (which selects the runtime
    /// default of clamp). Any other value is rejected by `fromJson` with
    /// a `qCWarning` and stored as empty, so the runtime falls back to
    /// the default rather than silently re-persisting the typo. Filter
    /// mode is not exposed here — both runtimes use linear filtering
    /// for user textures today. The slot index is the 0-based position
    /// in this list: the first entry binds to `uTexture1`, the second
    /// to `uTexture2`, etc. Up to three textures per effect; surplus
    /// entries are silently dropped at parse time.
    struct TextureSlot
    {
        QString path; ///< Filename relative to the effect's sourceDir.
        QString
            wrap; ///< "clamp" / "repeat" / "mirror"; empty = runtime default. Other values are rejected by fromJson.

        bool operator==(const TextureSlot& other) const
        {
            return path == other.path && wrap == other.wrap;
        }
        bool operator!=(const TextureSlot& other) const
        {
            return !(*this == other);
        }
    };
    QList<TextureSlot> textures;

    bool isValid() const
    {
        return !id.isEmpty() && !fragmentShaderPath.isEmpty();
    }

    QJsonObject toJson() const;
    static AnimationShaderEffect fromJson(const QJsonObject& obj);

    bool operator==(const AnimationShaderEffect& other) const;
    bool operator!=(const AnimationShaderEffect& other) const
    {
        return !(*this == other);
    }
};

/// True iff @p effect may meaningfully run on event @p path.
///
/// An effect with an empty `appliesTo` is universal and always returns
/// true on single-surface paths (the opt-in desktop and move classes are
/// excluded). Otherwise the predicate maps @p path to its event class via
/// `PhosphorAnimation::ProfilePaths::eventClassForPath` and checks
/// membership. A path with no determinable class (a mixed ancestor like
/// `window`, or a non-window/overlay path) also returns true — the
/// predicate only reports false when it can PROVE a mismatch, so it never
/// over-restricts a row whose class is ambiguous. The exceptions on
/// ambiguous rows are effects that provably cannot drive anything the row
/// cascades to: desktop-declaring packs (unbound second sampler) and
/// packs declaring neither geometry nor appearance (move-only — the move
/// leaf takes no inherited shader).
///
/// This is the (effect × path) analogue of
/// `PlasmaZones::eventPathSupportsShaderLeg(path)`, which gates whether a
/// path can run ANY shader. Both the settings picker (to dim incompatible
/// effects) and any future runtime verification consult this one predicate
/// so the policy has a single source of truth.
PHOSPHORANIMATION_EXPORT bool shaderEffectAppliesToEventPath(const AnimationShaderEffect& effect, const QString& path);

} // namespace PhosphorAnimationShaders

// Mark TextureSlot as relocatable so QList can move-construct entries
// in-place during reallocation rather than running the copy/move ctor
// per element. The struct is two QStrings; QString itself is already
// Q_RELOCATABLE_TYPE, so the aggregate is safely bitwise-relocatable.
// Must sit at file scope outside the namespace per Qt convention.
Q_DECLARE_TYPEINFO(PhosphorAnimationShaders::AnimationShaderEffect::TextureSlot, Q_RELOCATABLE_TYPE);
