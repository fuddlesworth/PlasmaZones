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
    double trailSeconds = 1.0;

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

    /// The pack's reach in logical px for the given friendly parameter map:
    /// the `reachParam` value when declared and present (falling back to
    /// that parameter's default), else `reach`. Clamped to
    /// `kMinReach`..`kMaxReach`.
    [[nodiscard]] double resolvedReach(const QVariantMap& params) const;
};

} // namespace PhosphorPointerShaders

Q_DECLARE_TYPEINFO(PhosphorPointerShaders::PointerShaderEffect::TextureSlot, Q_RELOCATABLE_TYPE);
Q_DECLARE_TYPEINFO(PhosphorPointerShaders::PointerShaderEffect::ParameterInfo, Q_RELOCATABLE_TYPE);
