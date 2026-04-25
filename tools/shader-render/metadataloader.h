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
    QString fragmentShader;            ///< .frag (required)
    QString vertexShader;              ///< .vert (optional)
    QString bufferShader;              ///< single-buffer multipass
    QStringList bufferShaders;         ///< multi-buffer multipass

    bool wallpaper      = false;
    bool multipass      = false;
    bool bufferFeedback = false;
    bool depthBuffer    = false;
    qreal bufferScale   = 1.0;
    QString bufferWrap   = QStringLiteral("clamp");
    QStringList bufferWraps;
    QString bufferFilter = QStringLiteral("linear");
    QStringList bufferFilters;

    // Default values seeded into the ShaderEffect:
    //
    //   customParams[0..7]  — vec4 array, x = float/int/bool slot value
    //   customColors[0..15] — color array
    //
    // Slots are filled per the metadata "parameters" entries.  Anything
    // unset stays at the ShaderEffect's "unset sentinel" (-1.0 vec4 /
    // transparent black) so the shader can detect it.
    std::array<QVector4D, 8> customParams = {};
    std::array<QColor, 16>   customColors = {};

    // For shaders that declare image-typed parameters; absolute paths.
    std::array<QString, 4> userTextures = {};
    std::array<QString, 4> userTextureWraps = {};
};

/**
 * @brief Parse data/shaders/<id>/metadata.json into ShaderMetadata.
 *
 * Returns false if the file is missing, malformed, or doesn't declare
 * a fragment shader.  All path fields are resolved absolute against
 * the metadata.json's directory.
 *
 * Parameter slot resolution mirrors the daemon: float/int/bool go into
 * customParams[slot].x, color → customColors[slot], image →
 * userTextures[slot] with wrap mode in userTextureWraps.
 */
bool loadShaderMetadata(const QString& metadataPath, ShaderMetadata& out);

} // namespace PlasmaZones::ShaderRender
