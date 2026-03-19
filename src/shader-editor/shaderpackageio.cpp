// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shaderpackageio.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcShaderEditorIO, "plasmazones.shadereditor.io")

namespace PlasmaZones {
namespace ShaderPackageIO {

ShaderPackageContents loadPackage(const QString& dirPath)
{
    ShaderPackageContents contents;
    contents.dirPath = dirPath;

    QDir dir(dirPath);
    if (!dir.exists()) {
        qCWarning(lcShaderEditorIO) << "Shader package directory does not exist:" << dirPath;
        return contents;
    }

    // Load metadata.json
    const QString metadataPath = dir.absoluteFilePath(QStringLiteral("metadata.json"));
    QFile metadataFile(metadataPath);
    if (metadataFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        contents.metadataJson = QString::fromUtf8(metadataFile.readAll());
        metadataFile.close();
    } else {
        qCWarning(lcShaderEditorIO) << "Failed to read metadata.json from:" << dirPath;
    }

    // Load all shader files (.vert, .frag, .glsl)
    const QStringList shaderFilters = {
        QStringLiteral("*.vert"),
        QStringLiteral("*.frag"),
        QStringLiteral("*.glsl"),
    };

    const QFileInfoList shaderFiles = dir.entryInfoList(shaderFilters, QDir::Files, QDir::Name);
    for (const QFileInfo& fileInfo : shaderFiles) {
        QFile file(fileInfo.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            ShaderFile sf;
            sf.filename = fileInfo.fileName();
            sf.content = QString::fromUtf8(file.readAll());
            contents.files.append(sf);
            file.close();
        } else {
            qCWarning(lcShaderEditorIO) << "Failed to read shader file:" << fileInfo.absoluteFilePath();
        }
    }

    qCDebug(lcShaderEditorIO) << "Loaded shader package from=" << dirPath
                              << "files=" << contents.files.size();
    return contents;
}

bool savePackage(const QString& dirPath, const ShaderPackageContents& contents)
{
    QDir dir(dirPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        qCWarning(lcShaderEditorIO) << "Failed to create directory:" << dirPath;
        return false;
    }

    // Write metadata.json
    {
        const QString path = dir.absoluteFilePath(QStringLiteral("metadata.json"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCWarning(lcShaderEditorIO) << "Failed to open metadata.json for writing:" << file.errorString();
            return false;
        }
        const QByteArray data = contents.metadataJson.toUtf8();
        if (file.write(data) != data.size()) {
            qCWarning(lcShaderEditorIO) << "Incomplete write to metadata.json:" << file.errorString();
            return false;
        }
        file.flush();
    }

    // Remove shader files that are no longer in the package
    {
        const QStringList shaderFilters = {
            QStringLiteral("*.vert"),
            QStringLiteral("*.frag"),
            QStringLiteral("*.glsl"),
        };
        QSet<QString> keepFiles;
        for (const ShaderFile& sf : contents.files) {
            keepFiles.insert(sf.filename);
        }
        const QFileInfoList existing = dir.entryInfoList(shaderFilters, QDir::Files);
        for (const QFileInfo& fi : existing) {
            if (!keepFiles.contains(fi.fileName())) {
                if (QFile::remove(fi.absoluteFilePath())) {
                    qCDebug(lcShaderEditorIO) << "Removed orphaned shader file:" << fi.fileName();
                } else {
                    qCWarning(lcShaderEditorIO) << "Failed to remove orphaned file:" << fi.absoluteFilePath();
                }
            }
        }
    }

    // Write shader files
    for (const ShaderFile& sf : contents.files) {
        if (sf.filename.contains(QLatin1Char('/')) || sf.filename.contains(QLatin1Char('\\'))) {
            qCWarning(lcShaderEditorIO) << "Rejecting filename with path separator:" << sf.filename;
            return false;
        }
        const QString path = dir.absoluteFilePath(sf.filename);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCWarning(lcShaderEditorIO) << "Failed to open for writing:" << path << file.errorString();
            return false;
        }
        const QByteArray data = sf.content.toUtf8();
        if (file.write(data) != data.size()) {
            qCWarning(lcShaderEditorIO) << "Incomplete write to:" << path << file.errorString();
            return false;
        }
        file.flush();
    }

    qCDebug(lcShaderEditorIO) << "Saved shader package to=" << dirPath << "files=" << contents.files.size();
    return true;
}

ShaderPackageContents createTemplate(const QString& shaderId, const QString& shaderName,
                                     ShaderFeatures features)
{
    ShaderPackageContents contents;
    const bool hasMultipass = features.testFlag(ShaderFeature::Multipass);
    const bool hasAudio = features.testFlag(ShaderFeature::AudioReactive);
    const bool hasWallpaper = features.testFlag(ShaderFeature::Wallpaper);
    const bool hasMouse = features.testFlag(ShaderFeature::MouseReactive);

    // ── Metadata ──
    QJsonObject metadata;
    metadata[QStringLiteral("id")] = shaderId;
    metadata[QStringLiteral("name")] = shaderName;
    metadata[QStringLiteral("category")] = QStringLiteral("Custom");
    metadata[QStringLiteral("description")] = QString();
    metadata[QStringLiteral("author")] = QString();
    metadata[QStringLiteral("version")] = QStringLiteral("1.0");
    metadata[QStringLiteral("fragmentShader")] = QStringLiteral("effect.frag");
    metadata[QStringLiteral("vertexShader")] = QStringLiteral("zone.vert");
    metadata[QStringLiteral("multipass")] = hasMultipass;
    if (hasMultipass) {
        metadata[QStringLiteral("bufferShaders")] = QJsonArray({QStringLiteral("pass0.frag")});
    }
    if (hasWallpaper) {
        metadata[QStringLiteral("wallpaper")] = true;
    }

    // ── Parameters (slots assigned sequentially to avoid collisions) ──
    QJsonArray params;
    int nextScalarSlot = 0;
    int nextColorSlot = 0;

    if (hasMultipass) {
        QJsonObject speed;
        speed[QStringLiteral("name")] = QStringLiteral("Speed");
        speed[QStringLiteral("type")] = QStringLiteral("float");
        speed[QStringLiteral("default")] = 1.0;
        speed[QStringLiteral("min")] = 0.0;
        speed[QStringLiteral("max")] = 5.0;
        speed[QStringLiteral("slot")] = nextScalarSlot++;
        params.append(speed);
    }
    if (hasAudio) {
        QJsonObject reactivity;
        reactivity[QStringLiteral("name")] = QStringLiteral("Reactivity");
        reactivity[QStringLiteral("type")] = QStringLiteral("float");
        reactivity[QStringLiteral("default")] = 1.0;
        reactivity[QStringLiteral("min")] = 0.0;
        reactivity[QStringLiteral("max")] = 3.0;
        reactivity[QStringLiteral("slot")] = nextScalarSlot++;
        params.append(reactivity);

        QJsonObject bassBoost;
        bassBoost[QStringLiteral("name")] = QStringLiteral("Bass Boost");
        bassBoost[QStringLiteral("type")] = QStringLiteral("float");
        bassBoost[QStringLiteral("default")] = 1.5;
        bassBoost[QStringLiteral("min")] = 0.0;
        bassBoost[QStringLiteral("max")] = 5.0;
        bassBoost[QStringLiteral("slot")] = nextScalarSlot++;
        params.append(bassBoost);

        QJsonObject colorShift;
        colorShift[QStringLiteral("name")] = QStringLiteral("Color Shift");
        colorShift[QStringLiteral("type")] = QStringLiteral("color");
        colorShift[QStringLiteral("default")] = QStringLiteral("#00ccff");
        colorShift[QStringLiteral("slot")] = nextColorSlot++;
        params.append(colorShift);
    }
    if (hasWallpaper) {
        QJsonObject blend;
        blend[QStringLiteral("name")] = QStringLiteral("Blend");
        blend[QStringLiteral("type")] = QStringLiteral("float");
        blend[QStringLiteral("default")] = 0.5;
        blend[QStringLiteral("min")] = 0.0;
        blend[QStringLiteral("max")] = 1.0;
        blend[QStringLiteral("slot")] = nextScalarSlot++;
        params.append(blend);

        QJsonObject tint;
        tint[QStringLiteral("name")] = QStringLiteral("Tint");
        tint[QStringLiteral("type")] = QStringLiteral("color");
        tint[QStringLiteral("default")] = QStringLiteral("#ffffff");
        tint[QStringLiteral("slot")] = nextColorSlot++;
        params.append(tint);
    }

    if (hasMouse) {
        QJsonObject influence;
        influence[QStringLiteral("name")] = QStringLiteral("Mouse Influence");
        influence[QStringLiteral("type")] = QStringLiteral("float");
        influence[QStringLiteral("default")] = 1.5;
        influence[QStringLiteral("min")] = 0.0;
        influence[QStringLiteral("max")] = 5.0;
        influence[QStringLiteral("slot")] = nextScalarSlot++;
        params.append(influence);

        QJsonObject glowRadius;
        glowRadius[QStringLiteral("name")] = QStringLiteral("Cursor Glow");
        glowRadius[QStringLiteral("type")] = QStringLiteral("float");
        glowRadius[QStringLiteral("default")] = 0.15;
        glowRadius[QStringLiteral("min")] = 0.0;
        glowRadius[QStringLiteral("max")] = 0.5;
        glowRadius[QStringLiteral("slot")] = nextScalarSlot++;
        params.append(glowRadius);

        QJsonObject cursorColor;
        cursorColor[QStringLiteral("name")] = QStringLiteral("Cursor Color");
        cursorColor[QStringLiteral("type")] = QStringLiteral("color");
        cursorColor[QStringLiteral("default")] = QStringLiteral("#ffffff");
        cursorColor[QStringLiteral("slot")] = nextColorSlot++;
        params.append(cursorColor);
    }

    metadata[QStringLiteral("parameters")] = params;
    QJsonDocument doc(metadata);
    contents.metadataJson = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));

    // ── zone.vert (always the same) ──
    ShaderFile vs;
    vs.filename = QStringLiteral("zone.vert");
    vs.content = QStringLiteral(
        "#version 450\n"
        "\n"
        "layout(location = 0) in vec2 position;\n"
        "layout(location = 1) in vec2 texCoord;\n"
        "\n"
        "layout(location = 0) out vec2 vTexCoord;\n"
        "layout(location = 1) out vec2 vFragCoord;\n"
        "\n"
        "#include <common.glsl>\n"
        "\n"
        "void main() {\n"
        "    vTexCoord = texCoord;\n"
        "    vFragCoord = vec2(texCoord.x, 1.0 - texCoord.y) * iResolution;\n"
        "    gl_Position = vec4(position, 0.0, 1.0);\n"
        "}\n");
    contents.files.append(vs);

    // ── Build GLSL accessor names for each parameter slot ──
    // In GLSL source code, custom parameters are accessed via the UBO arrays:
    //   customParams[vecIndex].component  (e.g. customParams[0].x for slot 0)
    //   customColors[colorSlot]           (e.g. customColors[0] for color slot 0)
    // NOTE: The C++ host side uses a different naming convention (computeUniformName):
    //   customParams1_x (1-indexed, underscore-separated) for setting values via the API.
    // These two naming schemes map to the same UBO data — do not confuse them.
    auto scalarAccessor = [](int slot) -> QString {
        static const char* comp[] = {"x", "y", "z", "w"};
        return QStringLiteral("customParams[%1].%2").arg(slot / 4).arg(QLatin1String(comp[slot % 4]));
    };
    auto colorAccessor = [](int slot) -> QString {
        return QStringLiteral("customColors[%1]").arg(slot);
    };

    int sc = 0, cc = 0; // slot cursors matching metadata param order
    QString speedA, reactivityA, bassBoostA, colorShiftA, blendA, tintA, mouseInflA, cursorGlowA, cursorColA;
    if (hasMultipass) { speedA = scalarAccessor(sc++); }
    if (hasAudio) { reactivityA = scalarAccessor(sc++); bassBoostA = scalarAccessor(sc++); colorShiftA = colorAccessor(cc++); }
    if (hasWallpaper) { blendA = scalarAccessor(sc++); tintA = colorAccessor(cc++); }
    if (hasMouse) { mouseInflA = scalarAccessor(sc++); cursorGlowA = scalarAccessor(sc++); cursorColA = colorAccessor(cc++); }

    // ── pass0.frag (only if multipass) ──
    if (hasMultipass) {
        ShaderFile pass0;
        pass0.filename = QStringLiteral("pass0.frag");
        pass0.content = QStringLiteral(
            "#version 450\n"
            "\n"
            "layout(location = 0) in vec2 vTexCoord;\n"
            "layout(location = 1) in vec2 vFragCoord;\n"
            "\n"
            "layout(location = 0) out vec4 fragColor;\n"
            "\n"
            "#include <common.glsl>\n"
            "#include <multipass.glsl>\n"
            "\n"
            "void main() {\n"
            "    vec2 uv = vFragCoord / iResolution;\n"
            "    float speed = %1 >= 0.0 ? %1 : 1.0; // Speed (default 1.0)\n"
            "\n"
            "    // Flow field: write velocity/data to buffer\n"
            "    float angle = atan(uv.y - 0.5, uv.x - 0.5) + iTime * speed;\n"
            "    vec2 flow = vec2(cos(angle), sin(angle));\n"
            "    float mag = 0.3 + 0.2 * sin(iTime * speed * 0.5);\n"
            "\n"
            "    // Encode: direction [-1,1] -> [0,1], magnitude in blue\n"
            "    fragColor = vec4(flow * 0.5 + 0.5, mag, 1.0);\n"
            "}\n").arg(speedA);
        contents.files.append(pass0);
    }

    // ── effect.frag (composed from active features) ──
    QString fragSrc;
    fragSrc += QStringLiteral(
        "#version 450\n"
        "\n"
        "layout(location = 0) in vec2 vTexCoord;\n"
        "layout(location = 1) in vec2 vFragCoord;\n"
        "\n"
        "layout(location = 0) out vec4 fragColor;\n"
        "\n"
        "#include <common.glsl>\n");
    if (hasMultipass) {
        fragSrc += QStringLiteral("#include <multipass.glsl>\n");
    }
    if (hasAudio) {
        fragSrc += QStringLiteral("#include <audio.glsl>\n");
    }
    if (hasWallpaper) {
        fragSrc += QStringLiteral("#include <wallpaper.glsl>\n");
    }

    // ── Helper function: render the effect for one zone ──
    fragSrc += QStringLiteral(
        "\n"
        "vec4 renderEffect(vec2 fragCoord, vec4 rect, vec4 fillColor,\n"
        "                   vec4 borderColor, vec4 params, bool isHighlighted");
    if (hasAudio) {
        fragSrc += QStringLiteral(", float bass, float mids, float treble, bool hasAudio");
    }
    fragSrc += QStringLiteral(
        ") {\n"
        "    vec2 pos = zoneRectPos(rect);\n"
        "    vec2 size = zoneRectSize(rect);\n"
        "    vec2 localUv = zoneLocalUV(fragCoord, pos, size);\n"
        "    vec2 center = fragCoord - pos - size * 0.5;\n"
        "    float borderRadius = max(params.x, 6.0);\n"
        "    float borderWidth = max(params.y, 1.5);\n"
        "\n"
        "    // Zone shape (rounded rectangle SDF)\n"
        "    float d = sdRoundedBox(center, size * 0.5, borderRadius);\n"
        "    if (d > 1.0) return vec4(0.0); // outside zone\n"
        "\n"
        "    float vitality = isHighlighted ? 1.0 : 0.3;\n");

    // ── Wallpaper ──
    if (hasWallpaper) {
        fragSrc += QStringLiteral(
            "\n"
            "    // Wallpaper sampling\n"
            "    vec2 wpSize = vec2(textureSize(uWallpaper, 0));\n"
            "    bool wpAvailable = wpSize.x > 1.0 && wpSize.y > 1.0;\n");
    }

    // ── Effect color ──
    if (hasAudio && !hasWallpaper && !hasMultipass) {
        // Audio-only: dark backdrop so spectrum bars are the focus
        fragSrc += QStringLiteral(
            "\n"
            "    // Dark backdrop for visualizer\n"
            "    vec3 col = fillColor.rgb * 0.08;\n");
    } else {
        fragSrc += QStringLiteral(
            "\n"
            "    // Animated gradient base\n"
            "    vec3 col = vec3(\n"
            "        0.5 + 0.5 * sin(localUv.x * 6.28 + iTime * vitality),\n"
            "        0.5 + 0.5 * sin(localUv.y * 6.28 + iTime * vitality * 1.3),\n"
            "        0.5 + 0.5 * sin((localUv.x + localUv.y) * 3.14 + iTime * vitality * 0.7));\n"
            "    col *= fillColor.rgb;\n");
    }

    if (hasMultipass) {
        fragSrc += QStringLiteral(
            "\n"
            "    // Buffer pass blend\n"
            "    vec4 buf = texture(iChannel0, channelUv(0, fragCoord));\n"
            "    vec2 flow = buf.rg * 2.0 - 1.0;\n"
            "    col += vec3(flow * 0.3, buf.b * 0.2) * vitality;\n");
    }

    if (hasWallpaper) {
        fragSrc += QStringLiteral(
            "\n"
            "    // Wallpaper blend\n"
            "    if (wpAvailable) {\n"
            "        vec2 wpUv = wallpaperUv(fragCoord, iResolution);\n"
            "        vec3 wp = texture(uWallpaper, wpUv).rgb * %1.rgb;\n"
            "        float blendVal = %2 >= 0.0 ? %2 : 0.5;\n"
            "        col = mix(col, wp, blendVal);\n"
            "    }\n").arg(tintA, blendA);
    }

    if (hasMouse) {
        fragSrc += QStringLiteral(
            "\n"
            "    // Mouse interaction\n"
            "    float mouseInfl = %1 >= 0.0 ? %1 : 1.5;\n"
            "    float cursorGlow = %2 >= 0.0 ? %2 : 0.15;\n"
            "    vec3 cursorCol = %3.rgb;\n"
            "    {\n"
            "        // Cursor position in zone-local UV\n"
            "        vec2 mouseUv = zoneLocalUV(iMouse.xy, pos, size);\n"
            "        bool mouseInZone = mouseUv.x >= 0.0 && mouseUv.x <= 1.0 &&\n"
            "                           mouseUv.y >= 0.0 && mouseUv.y <= 1.0;\n"
            "        if (mouseInZone) {\n"
            "            float dist = length(localUv - mouseUv);\n"
            "\n"
            "            // Gravitational lens: warp color toward cursor\n"
            "            float warpStr = exp(-dist * 8.0 / max(mouseInfl, 0.01)) * 0.05 * mouseInfl;\n"
            "            vec2 toCursor = normalize(mouseUv - localUv + vec2(0.001));\n"
            "            vec2 warpedUv = localUv + toCursor * warpStr;\n"
            "            float warpNoise = sin(warpedUv.x * 20.0 + iTime) * sin(warpedUv.y * 20.0 + iTime * 1.3);\n"
            "            col += col * warpNoise * warpStr * 3.0;\n"
            "\n"
            "            // Cursor glow: soft radial light\n"
            "            float glow = exp(-dist * dist / max(cursorGlow * cursorGlow, 0.001)) * cursorGlow * 2.0;\n"
            "            col += cursorCol * glow * vitality;\n"
            "\n"
            "            // Ripple ring expanding from cursor\n"
            "            float ripple = sin((dist - iTime * 0.5) * 30.0) * 0.5 + 0.5;\n"
            "            ripple *= exp(-dist * 6.0) * 0.15 * mouseInfl;\n"
            "            col += cursorCol * ripple * vitality;\n"
            "        }\n"
            "    }\n").arg(mouseInflA, cursorGlowA, cursorColA);
    }

    if (hasAudio) {
        fragSrc += QStringLiteral(
            "\n"
            "    // Audio spectrum histogram\n"
            "    float reactivity = %1 >= 0.0 ? %1 : 1.0;\n"
            "    float bassBoostVal = %2 >= 0.0 ? %2 : 1.5;\n"
            "    vec3 audioTint = %3.rgb;\n"
            "\n"
            "    {\n"
            "        // Y axis: 0 = bottom, 1 = top\n"
            "        float y = 1.0 - localUv.y;\n"
            "\n"
            "        // Discrete bar columns\n"
            "        float numBars = 20.0;\n"
            "        float barIndex = floor(localUv.x * numBars);\n"
            "        float barCenter = (barIndex + 0.5) / numBars;\n"
            "        float barEdge = abs(localUv.x - barCenter) * numBars;\n"
            "        float barGap = smoothstep(0.42, 0.3, barEdge);\n"
            "\n"
            "        // Bar height — use audio if available, otherwise idle bounce\n"
            "        float specVal;\n"
            "        if (hasAudio) {\n"
            "            specVal = audioBarSmooth(barCenter) * reactivity * 2.0;\n"
            "            specVal = pow(specVal, 0.7); // compress dynamic range so bars are visible\n"
            "            specVal *= (0.6 + bass * bassBoostVal);\n"
            "        } else {\n"
            "            // Idle: static bar silhouette so it's obvious this is a visualizer\n"
            "            specVal = 0.08 + 0.12 * sin(barCenter * 12.0 + 0.5);\n"
            "        }\n"
            "        specVal = clamp(specVal, 0.0, 0.95);\n"
            "\n"
            "        float barMask = step(y, specVal) * barGap;\n"
            "\n"
            "        // Color: gradient from tint at base to bright at peak\n"
            "        float peakRatio = y / max(specVal, 0.01);\n"
            "        vec3 barColor = mix(audioTint, audioTint * 2.0, peakRatio);\n"
            "        barColor = mix(barColor, vec3(1.0), smoothstep(0.8, 1.0, peakRatio) * 0.6);\n"
            "\n"
            "        col = mix(col, barColor, barMask * vitality);\n"
            "\n"
            "        // Bass glow on background\n"
            "        float glowAmount = hasAudio ? bass * bassBoostVal * 0.15 : 0.03;\n"
            "        col += audioTint * glowAmount * vitality;\n"
            "    }\n").arg(reactivityA, bassBoostA, colorShiftA);
    }

    // Zone border + final alpha
    fragSrc += QStringLiteral(
        "\n"
        "    // Border\n"
        "    float borderMask = smoothstep(0.0, 1.5, abs(d) - borderWidth);\n"
        "    col = mix(borderColor.rgb, col, borderMask);\n"
        "\n"
        "    // Zone alpha (anti-aliased edge)\n"
        "    float alpha = smoothstep(1.0, -0.5, d);\n"
        "    col = mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 0.4 + 0.6 * vitality);\n"
        "    return vec4(col, alpha);\n"
        "}\n");

    // ── Main function ──
    fragSrc += QStringLiteral(
        "\n"
        "void main() {\n"
        "    vec2 fragCoord = vFragCoord;\n"
        "    vec4 color = vec4(0.0);\n");

    if (hasAudio) {
        fragSrc += QStringLiteral(
            "\n"
            "    bool hasAudio = iAudioSpectrumSize > 0;\n"
            "    float bass = hasAudio ? getBassSoft() : 0.0;\n"
            "    float mids = hasAudio ? getMidsSoft() : 0.0;\n"
            "    float treble = hasAudio ? getTrebleSoft() : 0.0;\n");
    }

    fragSrc += QStringLiteral(
        "\n"
        "    for (int i = 0; i < zoneCount && i < 64; i++) {\n"
        "        vec4 rect = zoneRects[i];\n"
        "        if (rect.z <= 0.0 || rect.w <= 0.0) continue;\n"
        "\n"
        "        vec4 zoneColor = renderEffect(fragCoord, rect,\n"
        "            zoneFillColors[i], zoneBorderColors[i],\n"
        "            zoneParams[i], zoneParams[i].z > 0.5");
    if (hasAudio) {
        fragSrc += QStringLiteral(",\n            bass, mids, treble, hasAudio");
    }
    fragSrc += QStringLiteral(
        ");\n"
        "        color = blendOver(color, zoneColor);\n"
        "    }\n"
        "\n"
        "    // Zone labels overlay\n"
        "    color = compositeLabels(color, labelsUv(fragCoord), uZoneLabels);\n"
        "\n"
        "    fragColor = clampFragColor(color);\n"
        "}\n");

    ShaderFile frag;
    frag.filename = QStringLiteral("effect.frag");
    frag.content = fragSrc;
    contents.files.append(frag);

    qCDebug(lcShaderEditorIO) << "Created shader template id=" << shaderId
                              << "name=" << shaderName << "features=" << static_cast<int>(features);
    return contents;
}

QString userShaderDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasmazones/shaders");
}

QString systemShaderDirectory()
{
    return QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                  QStringLiteral("plasmazones/shaders"),
                                  QStandardPaths::LocateDirectory);
}

QString computeUniformName(const QString& type, int slot)
{
    if (type == QLatin1String("image")) {
        if (slot < 0 || slot > 3) {
            return {};
        }
        return QStringLiteral("uTexture%1").arg(slot);
    }

    if (type == QLatin1String("color")) {
        if (slot < 0 || slot > 15) {
            return {};
        }
        return QStringLiteral("customColor%1").arg(slot + 1);
    }

    // float, int, bool
    if (slot < 0 || slot > 31) {
        return {};
    }

    const int vecIndex = slot / 4;
    const int component = slot % 4;
    static const char* components[] = {"x", "y", "z", "w"};
    return QStringLiteral("customParams%1_%2").arg(vecIndex + 1).arg(QLatin1String(components[component]));
}

QString sanitizeId(const QString& name)
{
    QString id = name.toLower();
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
    static const QRegularExpression leadTrailDash(QStringLiteral("^-|-$"));
    id.replace(nonAlnum, QStringLiteral("-"));
    id.remove(leadTrailDash);
    if (id.isEmpty()) {
        id = QStringLiteral("custom-shader");
    }
    return id;
}

} // namespace ShaderPackageIO
} // namespace PlasmaZones
