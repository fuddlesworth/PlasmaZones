// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorSurface/phosphorsurface_export.h>

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace PhosphorSurfaceShaders {

/**
 * @brief Metadata for a single surface shader effect.
 *
 * Distinct from `PhosphorAnimationShaders::AnimationShaderEffect` (which
 * describes a finite-duration *transition* driven by a 0..1 timeline)
 * and from `PhosphorRendering::ShaderEffect` (which renders persistent
 * zone backgrounds). A `SurfaceShaderEffect` describes a persistent
 * per-window *surface layer* — the decoration band / border, rounded
 * corners, focus tint, etc. — composited over the live window content
 * for as long as the window is mapped.
 *
 * ## Identity
 *
 * Each effect is keyed by a stable `id` string (e.g. "border",
 * "rounded-corners"). Plugin-authored effects use reverse-domain or
 * namespaced ids ("myplugin.glow") to avoid collisions with built-in
 * effects.
 *
 * ## Shader source
 *
 * Each effect carries a single `fragmentShaderPath` — the same GLSL
 * source runs on both the compositor (KWin) and daemon (Qt RHI) surface-
 * layer paint paths; the runtime handles texture binding and coordinate
 * conventions, so separate per-backend shader variants are not needed.
 *
 * ## Parameters
 *
 * Each effect declares named parameters with type, default, min/max.
 * Field names mirror `AnimationShaderEffect::ParameterInfo` so surface
 * packs and animation packs can share QML editor components.
 *
 * ## Multipass buffer passes
 *
 * Surface shaders support opt-in multipass: when `isMultipass` is set and
 * `bufferShaderPaths` is non-empty, the daemon's surface-layer runtime
 * runs those buffer passes before the main fragment shader. The
 * kwin-effect compositor path is single-pass only; multipass effects
 * degrade to single-pass there with a diagnostic log, matching the
 * overlay / animation packs.
 *
 * ## Trimmed vs AnimationShaderEffect
 *
 * This struct mirrors the multipass / buffer fields that
 * `AnimationShaderEffect` carries, but still OMITS the wallpaper / depth-
 * extent-style event and `fboExtent` fields that only make sense for a
 * finite-duration transition, while keeping the identical identity,
 * shader-path, preview, parameter, and texture-slot shape.
 */
struct PHOSPHORSURFACE_EXPORT SurfaceShaderEffect
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

    /// Category for settings-UI grouping (e.g. "Border", "Corners", "Tint").
    QString category;

    /// Path to the fragment shader (relative to the effect dir). The same
    /// source is used on both the compositor and daemon paths — the
    /// runtime handles backend differences.
    QString fragmentShaderPath;

    /// Path to the vertex shader (relative to the effect dir). Empty = use
    /// the runtime's built-in fullscreen-quad vertex shader.
    QString vertexShaderPath;

    /// Resolved absolute directory containing this effect's assets.
    QString sourceDir;

    /// Whether this effect was loaded from a user-local directory.
    bool isUserEffect = false;

    /// Preview image path (relative to the effect dir). For settings UI.
    QString previewPath;

    // ── Multipass buffer passes (opt-in, matching overlay/animation packs) ──

    /// Opt-in multipass mode. When true and `bufferShaderPaths` is
    /// non-empty, the daemon path runs those buffer passes before the
    /// main fragment. The kwin-effect compositor path is single-pass
    /// only; multipass effects degrade to single-pass there with a
    /// diagnostic log (see `SurfaceShaderContract.h`).
    bool isMultipass = false;

    /// Names the parameter (an int/float, logical px) whose resolved value is
    /// the transparent OUTER MARGIN the pack needs around the surface to draw
    /// into — e.g. the glow pack's `glowSize`. The compositor host inflates
    /// the window's capture canvas by the chain's largest declared margin
    /// (per-window, from the surface's resolved parameter overrides) and
    /// presents on a matching padded quad, so an outer effect renders even
    /// when the window has no decoration-shadow margin of its own. The daemon
    /// host resolves the PRIMARY pack's margin the same way and publishes it
    /// as `decorationOuterPadding`; SurfaceDecoration inflates its capture
    /// sourceRect + shader item by it (OverlayService::applyDecoration).
    /// Empty = the pack draws within the surface (no padding requested).
    QString paddingParam;

    /// Declares that the pack references `iTime` and needs a per-frame
    /// driver. The compositor detects this itself (a linked iTime uniform
    /// gates the repaint), but the daemon UBO always carries iTime, so its
    /// hosts read this flag to decide whether to tick the item
    /// (SurfaceShaderItem `playing`). Static packs omit it and pay nothing.
    bool animated = false;

    /// Declares that the pack samples the scene BEHIND the window
    /// (backdropTexel() / uBackdrop). Compositor-only: the kwin effect
    /// captures the backdrop under the window's (padded) canvas each frame,
    /// routes the pack through the composite fold, and drives the window to
    /// repaint continuously. Daemon hosts have no scene behind their
    /// surfaces: uHasBackdrop stays 0 there and backdropTexel() returns
    /// transparent, so a pack must style a fallback on that gate.
    bool needsBackdrop = false;

    /// Declares that the pack draws a window border of its own (the border
    /// family: border, border-sweep, border-rgb, ...). Semantic contract
    /// flag, deliberately NOT keyed off the browsing category: a
    /// providesBorder pack is expected to declare the shared border param
    /// ids `borderWidth` / `cornerRadius` (and `activeColor` /
    /// `inactiveColor` where it distinguishes focus), which the settings
    /// side seeds from the plain Windows border setting when the pack is
    /// added to a chain. Any user pack in a window's chain already
    /// suppresses the plain border layer wholesale (see the kwin effect's
    /// updateWindowDecoration), so this flag gates seeding and UI hints,
    /// not rendering.
    bool providesBorder = false;

    /// Declares that the pack fades/tints the window surface (the reserved
    /// opacity-tint pack). Same contract idea as providesBorder: such a pack
    /// is expected to declare the shared param ids `opacity` /
    /// `tintStrength` / `tintColor`, which the settings side seeds from the
    /// plain Windows opacity+tint setting when the pack is added to a chain.
    /// Gates seeding, not rendering.
    bool providesOpacityTint = false;

    /// Declares that the pack reacts to the CAVA audio spectrum (it includes
    /// surface_audio.glsl and reads audioBar / getBass / …). Both runtimes feed
    /// it when the audio visualizer is on: the daemon pushes the live spectrum to
    /// a decorated OSD / popup carrying an audio pack, and the KWin effect runs
    /// its own CAVA provider to feed window decorations. Session-global like the
    /// overlay category. The flag is what lets each host gate CAVA on an audio
    /// pack being present, so a plain decoration never spins up audio capture.
    bool audio = false;

    /// Buffer-pass shader paths (relative to effect dir). When non-empty
    /// and `isMultipass` is true, the daemon's surface-layer runtime runs
    /// these as intermediate passes before the main fragment shader.
    QStringList bufferShaderPaths;

    /// Enable per-pass feedback (last frame's buffer is sampleable as
    /// `iChannel<N>`). Requires `isMultipass`. Daemon-only.
    bool bufferFeedback = false;

    /// Render-target scale relative to the surface size. Clamped to
    /// `[0.125, 1.0]` at `fromJson` time. Daemon-only — the compositor
    /// path doesn't allocate auxiliary FBOs.
    qreal bufferScale = 1.0;

    /// Default wrap mode for all buffer samplers. Sibling of
    /// `bufferWraps` (per-buffer overrides). Empty = runtime default.
    /// Daemon-only.
    QString bufferWrap;

    /// Per-buffer wrap-mode overrides; index aligns with
    /// `bufferShaderPaths`. Daemon-only.
    QStringList bufferWraps;

    /// Default filter mode for all buffer samplers. Empty = runtime
    /// default. Daemon-only.
    QString bufferFilter;

    /// Per-buffer filter-mode overrides; index aligns with
    /// `bufferShaderPaths`. Daemon-only.
    QStringList bufferFilters;

    /// Allocate a depth buffer alongside the colour FBO so the shader
    /// can sample window depth. Daemon-only.
    bool useDepthBuffer = false;

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

    /// Maximum number of buffer passes a pack may declare.
    ///
    /// Bounded because every declared pass costs a canvas-sized RGBA8 texture and a
    /// fullscreen draw PER DECORATED WINDOW, PER FRAME, and the count comes from an
    /// installable pack's JSON — an unvalidated system boundary, and previously the
    /// one uncapped axis (bufferScale, outerPadding and the texture slots are all
    /// bounded). Four is not arbitrary: the fold binds `iChannel0..3` and a pass
    /// samples only the passes before it, so a fifth buffer is structurally
    /// unreadable — allocated, cleared, drawn, and sampled by nothing.
    static constexpr int kMaxBufferPasses = 4;

    /// Declared shader inputs beyond the standard surface set
    /// (uTexture0, uSurfaceSize, uSurfaceFocused, etc.). Each entry maps
    /// `parameterId → { type, default, min, max, ... }`. Field names
    /// mirror the regular shader pack format
    /// (`AnimationShaderEffect::ParameterInfo` /
    /// `PhosphorRendering::ShaderRegistry::ParameterInfo`) so surface
    /// packs and overlay/animation packs can share QML editor components.
    ///
    /// **C++ field name vs JSON key asymmetry**: the QVariant fields are
    /// suffixed `Value` because `default` is a C++ keyword; the wire-
    /// format and QML-facing keys are the bare forms
    /// (`default`/`min`/`max`/`step`). See `toJson()` / `fromJson()` in
    /// `surfaceshadereffect.cpp` for the mapping.
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
    /// runtime's user-texture samplers (slot 0 / 1 / 2 here). The
    /// captured surface (`uTexture0`) is never user-declared.
    ///
    /// `path` is resolved relative to the effect's `sourceDir`. Loading
    /// failures are non-fatal — the effect still installs and the
    /// affected sampler reads transparent black; a `qCWarning` logs the
    /// missing file. `wrap` accepts only `"clamp"`, `"repeat"`,
    /// `"mirror"`, and the empty string (which selects the runtime
    /// default of clamp). Any other value is rejected by `fromJson` with
    /// a `qCWarning` and stored as empty. Up to three textures per
    /// effect; surplus entries are silently dropped at parse time.
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
    static SurfaceShaderEffect fromJson(const QJsonObject& obj);

    bool operator==(const SurfaceShaderEffect& other) const;
    bool operator!=(const SurfaceShaderEffect& other) const
    {
        return !(*this == other);
    }
};

} // namespace PhosphorSurfaceShaders

// Mark TextureSlot as relocatable so QList can move-construct entries
// in-place during reallocation rather than running the copy/move ctor
// per element. The struct is two QStrings; QString itself is already
// Q_RELOCATABLE_TYPE, so the aggregate is safely bitwise-relocatable.
// Must sit at file scope outside the namespace per Qt convention.
Q_DECLARE_TYPEINFO(PhosphorSurfaceShaders::SurfaceShaderEffect::TextureSlot, Q_RELOCATABLE_TYPE);
// ParameterInfo is five QStrings + four QVariants; both QString and QVariant are
// already Q_RELOCATABLE_TYPE, so the aggregate is safely bitwise-relocatable.
// Declared for the same QList<ParameterInfo> reallocation benefit as TextureSlot.
Q_DECLARE_TYPEINFO(PhosphorSurfaceShaders::SurfaceShaderEffect::ParameterInfo, Q_RELOCATABLE_TYPE);
