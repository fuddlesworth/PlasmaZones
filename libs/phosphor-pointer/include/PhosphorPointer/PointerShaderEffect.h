// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorPointer/phosphorpointer_export.h>

#include <PhosphorShaders/CustomParamsKey.h>

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace PhosphorPointerShaders {

/**
 * @brief Metadata for a single pointer shader effect.
 *
 * A pointer effect decorates the mouse pointer (trail, halo, click ripple,
 * sparks). It is a continuous decoration: the host ticks it every frame while
 * the pointer has recent motion or a recent button event, for `trailSeconds`
 * after the last one, then goes quiet.
 *
 * Field names of `ParameterInfo` and `TextureSlot` mirror the surface and
 * animation families so the settings app's ParameterEditor works unchanged.
 */
struct PHOSPHORPOINTER_EXPORT PointerShaderEffect
{
    /// Where the chain paints relative to the cursor.
    enum class Layer {
        /// The chain paints under KWin's cursor (or the hardware cursor
        /// plane) and never touches cursor visibility.
        Below,
        /// The host hides KWin's cursor while the pack is live on the output
        /// under the pointer and paints the cursor sprite itself after the
        /// chain.
        Above,
    };

    /// The metadata spelling of @p layer, the inverse of what `fromJson`
    /// parses. A consumer that shows the value to a user or hands it to QML
    /// reads back the word the pack author wrote rather than an integer it
    /// would have to decode. Lives here, beside the enum and the parser, so
    /// the two spellings cannot drift apart and so no consumer has to keep a
    /// private copy of the mapping.
    static QString layerToken(Layer layer);

    /// Stable identifier, the registry lookup key.
    QString id;

    /// Human-readable display name.
    QString name;

    /// One-line description for settings UI.
    QString description;

    /// Author attribution.
    QString author;

    /// Semantic version of this pack.
    QString version;

    /// Category for settings-UI grouping ("Trail", "Glow", "Click", ...).
    QString category;

    /// Fragment shader path. Relative to the pack dir as authored; resolved
    /// to an absolute path confined to `sourceDir` by `fromJson` when a
    /// source dir is supplied.
    QString fragmentShaderPath;

    /// Optional vertex shader path (same resolution rules). Empty = the
    /// runtime's built-in fullscreen-quad vertex stage.
    QString vertexShaderPath;

    /// Resolved absolute directory containing this pack's assets.
    QString sourceDir;

    /// Whether this pack was loaded from the user-local directory.
    bool isUserEffect = false;

    /// Preview image path (absolute once resolved). Empty when none.
    QString previewPath;

    /// Paint layer relative to the cursor. Default Below.
    Layer layer = Layer::Below;

    /// How far, in logical px, from any trail point or press point the pack
    /// may paint. The host derives the damage rect from it, so a pack that
    /// paints outside its reach gets clipped. Default 64.
    double reach = 64.0;

    /// Optional name of an int/float parameter whose resolved value replaces
    /// `reach` (same idea as the surface family's `paddingParam`).
    QString reachParam;

    /// How long, in seconds, after the last motion or button event the pack
    /// still needs frames. Default 1.0.
    ///
    /// This is a LIVENESS figure and nothing else. It says how long the host
    /// keeps painting, not how the sampler spaces its slots — see
    /// `samplesTrail` for why the two have to stay apart.
    double trailSeconds = 1.0;

    /// Whether any of the pack's stages actually reads `uPointerTrail`.
    ///
    /// The host spreads the history's fixed number of slots over the longest
    /// WINDOW in the chain (see `resolvedTrailWindow`), so one pack's answer
    /// sets the spacing for every pack sharing the ring. A click pack that
    /// draws only at the press point still needs a long `trailSeconds` to keep
    /// its animation running, and would otherwise coarsen the stroke of every
    /// trail pack beside it while reading no samples of its own. Declaring
    /// this false keeps such a pack out of the sampling decision without
    /// shortening the frames it gets.
    ///
    /// Defaults TRUE, which is the conservative answer: a pack that says
    /// nothing is assumed to read the trail and still has a say in the
    /// spacing. The validator cross-checks the declaration against the stage
    /// sources, so it cannot quietly drift away from what the shaders do.
    bool samplesTrail = true;

    /// How far back, in seconds, the pack's stages actually READ the trail.
    /// Zero means "not declared", and the window falls back to
    /// `trailSeconds`.
    ///
    /// `samplesTrail` says WHETHER a pack reads the ring; this says HOW MUCH
    /// of it. The two are different questions and the second one is what
    /// should set the spacing, because the host spreads a fixed number of
    /// slots across the window it is given. Taking that window from
    /// `trailSeconds` charges every pack for its LIVENESS: a pack that must
    /// keep drawing for two seconds after the pointer stops, but only ever
    /// looks at the last half second of path, was getting its stroke sampled
    /// four times more coarsely than it needed.
    double trailWindowSeconds = 0.0;

    /// Optional name of an int/float parameter whose resolved value replaces
    /// `trailWindowSeconds`, the same shape as `reachParam` over `reach`.
    ///
    /// This is the form most packs want, because how far back they read is
    /// usually a slider: a trail's `lifetime`, a comet's `length`. Following
    /// the resolved value means a user who shortens the tail gets a finer
    /// stroke for it, instead of paying the spacing for a length they are not
    /// using.
    QString trailWindowParam;

    /// Bind the cursor sprite as `uCursorSprite`. Default false.
    bool needsCursor = false;

    /// Opt-in multipass. Normalised to false when no buffer shader survives
    /// resolution.
    bool isMultipass = false;

    /// Buffer-pass shader paths (max `kMaxBufferPasses`), resolved like
    /// `fragmentShaderPath`.
    QStringList bufferShaderPaths;

    /// Last frame's buffer is sampleable as `iChannel<N>`. Requires
    /// `isMultipass`.
    bool bufferFeedback = false;

    /// Render-target scale relative to the canvas, clamped to
    /// [`kMinBufferScale`, `kMaxBufferScale`] at parse time.
    double bufferScale = 1.0;

    static constexpr double kMinBufferScale = PhosphorShaders::kMinBufferScale;
    static constexpr double kMaxBufferScale = PhosphorShaders::kMaxBufferScale;

    /// Bounds on `resolvedReach`, in logical px. The floor exists because a
    /// reach of 0 is never what a pack means: the damage rect is the trail's
    /// bounding box inflated by the reach, so at 0 a single-sample burst (one
    /// event, no motion since) gives a rect with no area, the pass stays
    /// live for `trailSeconds` and paints nothing into it. One logical px is
    /// the smallest reach that still turns a point into a region.
    static constexpr double kMinReach = 1.0;
    static constexpr double kMaxReach = 1024.0;

    /// Declared parameter. JSON keys are the bare `default` / `min` / `max`
    /// / `step`; the C++ fields carry a `Value` suffix because `default` is
    /// a keyword. Textures are not parameters: they are declared through the
    /// top-level `textures` array and reach the shader as `uTexture<N>`.
    struct ParameterInfo
    {
        QString id;
        QString name;
        QString type; ///< "float", "int", "bool", "color"
        QString description;
        QString group;
        QVariant defaultValue;
        QVariant minValue;
        QVariant maxValue;
        QVariant stepValue;
    };
    QList<ParameterInfo> parameters;

    /// User texture slot. `path` is resolved relative to `sourceDir`; `wrap`
    /// is "clamp" / "repeat" / "mirror" or empty for the runtime default.
    struct TextureSlot
    {
        QString path;
        QString wrap;

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

    /// Parse a pack's `metadata.json` root. When @p sourceDir is non-empty
    /// every shader / texture / preview path is resolved against it and
    /// confined to it (a `..` traversal or symlink escape clears the path,
    /// which fail-closes the pack). When empty the paths are kept verbatim
    /// for an in-memory effect.
    static PointerShaderEffect fromJson(const QJsonObject& obj, const QString& sourceDir = QString(),
                                        bool isUser = false);

    bool isValid() const
    {
        return !id.isEmpty() && !fragmentShaderPath.isEmpty();
    }

    /// How far back this pack reads the trail, in seconds, for the given
    /// friendly parameter map: the `trailWindowParam` value when declared and
    /// present (falling back to that parameter's default), else
    /// `trailWindowSeconds`, else `trailSeconds`. Clamped to `trailSeconds`,
    /// since a pack cannot read further back than the host keeps it alive,
    /// and to a non-negative value.
    ///
    /// Returns 0 when `samplesTrail` is false: such a pack reads nothing and
    /// has no business setting the spacing at all.
    [[nodiscard]] double resolvedTrailWindow(const QVariantMap& params) const;

    /// The pack's reach in logical px for the given friendly parameter map:
    /// the `reachParam` value when declared and present (falling back to
    /// that parameter's default), else `reach`. Clamped to
    /// `kMinReach`..`kMaxReach`.
    [[nodiscard]] double resolvedReach(const QVariantMap& params) const;

private:
    /// The value of the int/float parameter @p paramId from @p params, its
    /// declared default when the map has no entry, and @p fallback when the
    /// name is empty, undeclared or of a type with no number in it. Shared by
    /// `resolvedReach` and `resolvedTrailWindow`, which differ only in what
    /// they clamp the answer to.
    [[nodiscard]] double resolvedParam(const QString& paramId, const QVariantMap& params, double fallback) const;
};

} // namespace PhosphorPointerShaders

Q_DECLARE_TYPEINFO(PhosphorPointerShaders::PointerShaderEffect::TextureSlot, Q_RELOCATABLE_TYPE);
Q_DECLARE_TYPEINFO(PhosphorPointerShaders::PointerShaderEffect::ParameterInfo, Q_RELOCATABLE_TYPE);
