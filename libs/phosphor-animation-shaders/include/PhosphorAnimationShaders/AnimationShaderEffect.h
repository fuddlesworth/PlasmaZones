// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimationShaders/phosphoranimationshaders_export.h>

#include <QJsonObject>
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
 * ## Shader variants
 *
 * An effect carries two optional shader paths:
 * - `fragmentShaderPath`: the QtQuick / overlay variant (GLSL 330+)
 * - `kwinFragmentShaderPath`: the KWin compositor variant (may differ
 *   due to texture-input conventions)
 *
 * A consumer picks the variant it needs. Missing variant = effect is
 * unavailable on that rendering path.
 *
 * ## Parameters
 *
 * Each effect declares named parameters with type, default, min/max.
 * Per-event parameter overrides live on `ShaderProfile`, not here.
 */
struct PHOSPHORANIMATIONSHADERS_EXPORT AnimationShaderEffect
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

    /// Path to the QtQuick-side fragment shader (relative to the effect dir).
    QString fragmentShaderPath;

    /// Path to the vertex shader (relative to the effect dir). Empty = use
    /// the library's built-in fullscreen-quad vertex shader.
    QString vertexShaderPath;

    /// Path to the KWin compositor-side fragment shader (relative to the
    /// effect dir). Empty = effect not available on the KWin path.
    QString kwinFragmentShaderPath;

    /// Resolved absolute directory containing this effect's assets.
    QString sourceDir;

    /// Whether this effect was loaded from a user-local directory.
    bool isUserEffect = false;

    /// Preview image path (relative to the effect dir). For settings UI.
    QString previewPath;

    /// Declared shader inputs beyond the standard set (iTime, iFrame, etc.).
    /// Each entry maps `parameterId → { type, default, min, max, ... }`.
    struct ParameterInfo
    {
        QString id;
        QString name;
        QString type; ///< "float", "int", "bool", "color"
        QVariant defaultValue;
        QVariant minValue;
        QVariant maxValue;
    };
    QList<ParameterInfo> parameters;

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
