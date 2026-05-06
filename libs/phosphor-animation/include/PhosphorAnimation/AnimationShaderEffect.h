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

    /// How much to enlarge the shader effect's bounding box beyond the
    /// anchor's logical size, expressed as a fraction of the anchor's
    /// width/height. The shader effect is positioned so the anchor sits
    /// in the centre and the padding extends symmetrically outward;
    /// shaders that distort their silhouette outside the original
    /// rectangle (e.g. morph's UV warp) need padding > 0 so the rippled
    /// silhouette has room to render before being clipped by the
    /// shader effect's own bounding box. Default 0.0 — shader effect
    /// matches the anchor exactly. Shaders that opt in must remap
    /// vTexCoord through this padding to recover the anchor-space UV
    /// (see data/animations/morph/effect.frag for the canonical pattern).
    qreal boundsPadding = 0.0;

    /// How wide the shader effect's render target is — relative to its
    /// anchor (default) or its anchor's parent item.
    ///
    ///   • `Anchor` (default): FBO covers `anchor + 2 · boundsPadding ·
    ///     anchor` on each axis. iResolution equals the FBO size, so
    ///     `vTexCoord` 0..1 spans the captured anchor texture 1:1.
    ///     Existing fragment-only effects (morph, dissolve, glitch, …)
    ///     all use this.
    ///
    ///   • `Parent`: FBO covers the anchor's parent item (the QML scene
    ///     root for OSDs and popups, both screen-sized post-Phase-6
    ///     fullscreen surface migration). iResolution equals the captured
    ///     anchor's pixel size — NOT the FBO. The shader is responsible
    ///     for using `iSurfaceScreenPos` + `iResolution` to position the
    ///     card-sized quad within the parent-sized FBO via its vertex
    ///     stage. This unlocks vertex-shader effects that translate the
    ///     surface across the screen (fly-in from edge, slide, lift) —
    ///     they're constrained to the FBO bounds by definition, so the
    ///     FBO has to span the screen for the motion to read as real
    ///     screen-scale travel.
    ///
    /// Authors who pick `Parent` MUST ship a custom vertex shader that
    /// remaps `position` to the card's region within the FBO; the
    /// default identity vertex stage stretches the card texture across
    /// the entire screen. See `data/animations/fly-in/effect.vert` for
    /// the canonical positioning pattern.
    enum class BoundsExtent {
        Anchor,
        Parent,
    };
    BoundsExtent boundsExtent = BoundsExtent::Anchor;

    /// Hard ceiling on `boundsPadding`. SHARED source-of-truth between
    /// the metadata clamp in `AnimationShaderEffect::fromJson` /
    /// `toJson` and the runtime clamp in
    /// `surfaceanimator.cpp::kMaxBoundsPaddingFraction`. Drift between
    /// the two would silently allow a programmatic in-memory effect to
    /// exceed the FBO-size budget that the metadata clamp enforces.
    /// At pad=2.0 the shader effect is 5x the anchor on each axis
    /// (k=1+2*pad=5) -> 25x area; a 1080p RGBA8 FBO at that scale
    /// is already ~200 MB, the edge of what Vulkan validation passes
    /// on integrated GPUs. No shipping shader needs >1.0 (morph uses
    /// 0.5); 2.0 accommodates plugin authors with extreme silhouette
    /// warps without permitting prior 4.0-cap excess (9x axis = 81x
    /// area).
    static constexpr qreal kMaxBoundsPadding = 2.0;

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
    /// canonical samplers `iChannel1` / `iChannel2` / `iChannel3` (slots
    /// 0 / 1 / 2 here, which the runtimes map to texture-unit
    /// allocations 1 / 2 / 3 — slot 0 of the binding-7+ region is the
    /// surface itself / `iChannel0`, never user-declared).
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
    /// in this list: the first entry binds to `iChannel1`, the second
    /// to `iChannel2`, etc. Up to three textures per effect; surplus
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

} // namespace PhosphorAnimationShaders

// Mark TextureSlot as relocatable so QList can move-construct entries
// in-place during reallocation rather than running the copy/move ctor
// per element. The struct is two QStrings; QString itself is already
// Q_RELOCATABLE_TYPE, so the aggregate is safely bitwise-relocatable.
// Must sit at file scope outside the namespace per Qt convention.
Q_DECLARE_TYPEINFO(PhosphorAnimationShaders::AnimationShaderEffect::TextureSlot, Q_RELOCATABLE_TYPE);
