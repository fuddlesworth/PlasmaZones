// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector4D>

#include <array>

namespace PlasmaZones::ShaderRender {

/**
 * @brief Subset of data/shaders/<id>/metadata.json the renderer needs.
 *
 * Mirrors the fields the daemon reads when constructing a ShaderEffect.
 * The bake / hot-reload bits aren't relevant here — every render is a
 * cold start, with the parameter array used to seed customParams +
 * customColors before frame 0.
 */
struct ShaderMetadata
{
    QString id;
    QString name;
    QString category;
    QString description;
    QString author;
    QString version;

    // Resolved absolute paths.  metadataloader fills these from the
    // metadata.json's directory + relative filename fields.
    QString fragmentShader; ///< .frag (required)
    QString vertexShader; ///< .vert (optional)
    QString bufferShader; ///< single-buffer multipass
    QStringList bufferShaders; ///< multi-buffer multipass

    bool wallpaper = false;
    bool multipass = false;
    bool bufferFeedback = false;
    bool depthBuffer = false;
    qreal bufferScale = 1.0;
    QString bufferWrap = QStringLiteral("clamp");
    QStringList bufferWraps;
    QString bufferFilter = QStringLiteral("linear");
    QStringList bufferFilters;

    // Default values seeded into the ShaderEffect.  Mirror ShaderEffect's
    // internal init exactly so unset slots take the same code path the
    // runtime takes:
    //   * customParams components default to -1.0 ("unset" sentinel).  The
    //     GLSL pattern `customParams[i].x >= 0.0 ? value : fallback` reads
    //     0 as "set to zero" and skips the fallback — initializing to 0
    //     would silently override every shader's intended default.
    //   * customColors default to transparent black (0,0,0,0).  An invalid
    //     QColor (the std::array default) doesn't match the runtime and
    //     setCustomColorAt would happily forward it to the shader.
    static constexpr QVector4D kUnsetParam{-1.0f, -1.0f, -1.0f, -1.0f};
    std::array<QVector4D, 8> customParams = {{
        kUnsetParam,
        kUnsetParam,
        kUnsetParam,
        kUnsetParam,
        kUnsetParam,
        kUnsetParam,
        kUnsetParam,
        kUnsetParam,
    }};
    std::array<QColor, 16> customColors = {{
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
        QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f),
    }};

    // For shaders that declare image-typed parameters; absolute paths.
    std::array<QString, 4> userTextures = {};
    std::array<QString, 4> userTextureWraps = {};
};

/**
 * @brief Parse data/shaders/<id>/metadata.json into ShaderMetadata.
 *
 * Returns false if the file is missing, malformed, doesn't declare a
 * fragment shader, or the declared fragment shader does not exist on
 * disk. All path fields are resolved absolute against the metadata.json's
 * directory.
 *
 * Parameter slot resolution mirrors the daemon's flat 0–31 slot index:
 *   * float / int / bool — slot S → customParams[S/4].(x|y|z|w)[S%4]
 *     (8 vec4s × 4 channels = 32 distinct float slots)
 *   * color — slot S → customColors[S] (16 slots)
 *   * image — slot S → userTextures[S] / userTextureWraps[S] (4 slots)
 *
 * Parameters without a `slot` field are skipped silently (treated as
 * UI-only metadata). Parameters with an explicitly invalid slot
 * (negative or out-of-range for the type) are dropped with a warning.
 */
bool loadShaderMetadata(const QString& metadataPath, ShaderMetadata& out);

} // namespace PlasmaZones::ShaderRender
