// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../zoneshaderitem.h"

#include "../../../core/constants.h"
#include "../../../core/logging.h"

#include <QFile>
#include <QMutexLocker>
#include <QPainter>
#include <QSvgRenderer>

namespace PlasmaZones {

// ============================================================================
// Zone Data Setters
// ============================================================================

void ZoneShaderItem::setZones(const QVariantList& zones)
{
    if (m_zones == zones) {
        return;
    }

    // Capture old counts before update
    const int oldZoneCount = m_zoneCount;
    const int oldHighlightedCount = m_highlightedCount;

    m_zones = zones;
    parseZoneData();

    Q_EMIT zonesChanged();

    // Only emit count signals if counts actually changed
    if (m_zoneCount != oldZoneCount) {
        Q_EMIT zoneCountChanged();
    }
    if (m_highlightedCount != oldHighlightedCount) {
        Q_EMIT highlightedCountChanged();
    }

    update();
}

void ZoneShaderItem::setHoveredZoneIndex(int index)
{
    // Clamp to valid range: -1 (none) or 0..(zoneCount-1)
    const int clamped = (index < 0 || index >= m_zones.size()) ? -1 : index;
    if (m_hoveredZoneIndex == clamped) {
        return;
    }
    m_hoveredZoneIndex = clamped;
    if (!m_zones.isEmpty()) {
        updateHoveredHighlightOnly(); // Lightweight: only update highlight flags, avoids full parse/sync
    }
    Q_EMIT hoveredZoneIndexChanged();
    update();
}

// ============================================================================
// Labels Texture
// ============================================================================

QImage ZoneShaderItem::labelsTexture() const
{
    QMutexLocker lock(&m_labelsTextureMutex);
    return m_labelsTexture;
}

void ZoneShaderItem::setLabelsTexture(const QImage& image)
{
    QImage newImage = image;
    {
        QMutexLocker lock(&m_labelsTextureMutex);
        if (m_labelsTexture.cacheKey() == newImage.cacheKey()) {
            return;
        }
        m_labelsTexture = std::move(newImage);
    }
    Q_EMIT labelsTextureChanged();
    update();
}

// ============================================================================
// Shader Params (complex parsing — customParams, customColors, uTextures)
// ============================================================================

void ZoneShaderItem::setShaderParams(const QVariantMap& params)
{
    if (shaderParams() == params) {
        return;
    }

    // Call parent to store and emit
    PhosphorRendering::ShaderEffect::setShaderParams(params);

    // Extract float params: customParams1_x through customParams8_w (slots 0-31)
    auto extractFloat = [&params](const QString& key, float defaultVal) -> float {
        if (params.contains(key)) {
            bool ok = false;
            float val = params.value(key).toFloat(&ok);
            return ok ? val : defaultVal;
        }
        return defaultVal;
    };

    // Extract and apply custom params
    QVector4D cp1 = customParams1();
    cp1.setX(extractFloat(QStringLiteral("customParams1_x"), cp1.x()));
    cp1.setY(extractFloat(QStringLiteral("customParams1_y"), cp1.y()));
    cp1.setZ(extractFloat(QStringLiteral("customParams1_z"), cp1.z()));
    cp1.setW(extractFloat(QStringLiteral("customParams1_w"), cp1.w()));
    setCustomParams1(cp1);

    QVector4D cp2 = customParams2();
    cp2.setX(extractFloat(QStringLiteral("customParams2_x"), cp2.x()));
    cp2.setY(extractFloat(QStringLiteral("customParams2_y"), cp2.y()));
    cp2.setZ(extractFloat(QStringLiteral("customParams2_z"), cp2.z()));
    cp2.setW(extractFloat(QStringLiteral("customParams2_w"), cp2.w()));
    setCustomParams2(cp2);

    QVector4D cp3 = customParams3();
    cp3.setX(extractFloat(QStringLiteral("customParams3_x"), cp3.x()));
    cp3.setY(extractFloat(QStringLiteral("customParams3_y"), cp3.y()));
    cp3.setZ(extractFloat(QStringLiteral("customParams3_z"), cp3.z()));
    cp3.setW(extractFloat(QStringLiteral("customParams3_w"), cp3.w()));
    setCustomParams3(cp3);

    QVector4D cp4 = customParams4();
    cp4.setX(extractFloat(QStringLiteral("customParams4_x"), cp4.x()));
    cp4.setY(extractFloat(QStringLiteral("customParams4_y"), cp4.y()));
    cp4.setZ(extractFloat(QStringLiteral("customParams4_z"), cp4.z()));
    cp4.setW(extractFloat(QStringLiteral("customParams4_w"), cp4.w()));
    setCustomParams4(cp4);

    QVector4D cp5 = customParams5();
    cp5.setX(extractFloat(QStringLiteral("customParams5_x"), cp5.x()));
    cp5.setY(extractFloat(QStringLiteral("customParams5_y"), cp5.y()));
    cp5.setZ(extractFloat(QStringLiteral("customParams5_z"), cp5.z()));
    cp5.setW(extractFloat(QStringLiteral("customParams5_w"), cp5.w()));
    setCustomParams5(cp5);

    QVector4D cp6 = customParams6();
    cp6.setX(extractFloat(QStringLiteral("customParams6_x"), cp6.x()));
    cp6.setY(extractFloat(QStringLiteral("customParams6_y"), cp6.y()));
    cp6.setZ(extractFloat(QStringLiteral("customParams6_z"), cp6.z()));
    cp6.setW(extractFloat(QStringLiteral("customParams6_w"), cp6.w()));
    setCustomParams6(cp6);

    QVector4D cp7 = customParams7();
    cp7.setX(extractFloat(QStringLiteral("customParams7_x"), cp7.x()));
    cp7.setY(extractFloat(QStringLiteral("customParams7_y"), cp7.y()));
    cp7.setZ(extractFloat(QStringLiteral("customParams7_z"), cp7.z()));
    cp7.setW(extractFloat(QStringLiteral("customParams7_w"), cp7.w()));
    setCustomParams7(cp7);

    QVector4D cp8 = customParams8();
    cp8.setX(extractFloat(QStringLiteral("customParams8_x"), cp8.x()));
    cp8.setY(extractFloat(QStringLiteral("customParams8_y"), cp8.y()));
    cp8.setZ(extractFloat(QStringLiteral("customParams8_z"), cp8.z()));
    cp8.setW(extractFloat(QStringLiteral("customParams8_w"), cp8.w()));
    setCustomParams8(cp8);

    // Color params: customColor1-16
    auto extractColor = [&params](const QString& key, const QColor& defaultVal) -> QColor {
        if (params.contains(key)) {
            QVariant val = params.value(key);
            if (val.canConvert<QColor>()) {
                return val.value<QColor>();
            }
            if (val.typeId() == QMetaType::QString) {
                QColor color(val.toString());
                if (color.isValid()) {
                    return color;
                }
            }
        }
        return defaultVal;
    };

    static const char* const colorKeys[] = {
        "customColor1",  "customColor2",  "customColor3",  "customColor4",  "customColor5",  "customColor6",
        "customColor7",  "customColor8",  "customColor9",  "customColor10", "customColor11", "customColor12",
        "customColor13", "customColor14", "customColor15", "customColor16",
    };

    // Array of setter function pointers to avoid switch
    using ColorSetter = void (ShaderEffect::*)(const QColor&);
    static const ColorSetter colorSetters[] = {
        &ShaderEffect::setCustomColor1,  &ShaderEffect::setCustomColor2,  &ShaderEffect::setCustomColor3,
        &ShaderEffect::setCustomColor4,  &ShaderEffect::setCustomColor5,  &ShaderEffect::setCustomColor6,
        &ShaderEffect::setCustomColor7,  &ShaderEffect::setCustomColor8,  &ShaderEffect::setCustomColor9,
        &ShaderEffect::setCustomColor10, &ShaderEffect::setCustomColor11, &ShaderEffect::setCustomColor12,
        &ShaderEffect::setCustomColor13, &ShaderEffect::setCustomColor14, &ShaderEffect::setCustomColor15,
        &ShaderEffect::setCustomColor16,
    };

    using ColorGetter = QColor (ShaderEffect::*)() const;
    static const ColorGetter colorGetters[] = {
        &ShaderEffect::customColor1,  &ShaderEffect::customColor2,  &ShaderEffect::customColor3,
        &ShaderEffect::customColor4,  &ShaderEffect::customColor5,  &ShaderEffect::customColor6,
        &ShaderEffect::customColor7,  &ShaderEffect::customColor8,  &ShaderEffect::customColor9,
        &ShaderEffect::customColor10, &ShaderEffect::customColor11, &ShaderEffect::customColor12,
        &ShaderEffect::customColor13, &ShaderEffect::customColor14, &ShaderEffect::customColor15,
        &ShaderEffect::customColor16,
    };

    for (int i = 0; i < 16; ++i) {
        QColor current = (this->*colorGetters[i])();
        QColor extracted = extractColor(QString::fromLatin1(colorKeys[i]), current);
        (this->*colorSetters[i])(extracted);
    }

    // User texture params: uTexture0-3 paths, wrap modes, and SVG render sizes
    for (int i = 0; i < 4; ++i) {
        // SVG render size (must be parsed before path, since path loading uses it)
        const QString sizeKey = QStringLiteral("uTexture%1_svgSize").arg(i);
        const bool svgSizeChanged = params.contains(sizeKey);
        if (svgSizeChanged) {
            m_userTextureSvgSizes[i] = qBound(64, params.value(sizeKey).toInt(), 4096);
        }

        const QString texKey = QStringLiteral("uTexture%1").arg(i);
        const bool hasTexKey = params.contains(texKey);
        const QString path = hasTexKey ? params.value(texKey).toString() : m_userTexturePaths[i];
        const bool pathChanged = hasTexKey && (m_userTexturePaths[i] != path);
        const bool needsReload = pathChanged || (svgSizeChanged && !m_userTexturePaths[i].isEmpty());

        if (hasTexKey) {
            m_userTexturePaths[i] = path;
        }

        if (needsReload) {
            if (!path.isEmpty() && QFile::exists(path)) {
                const bool isSvg = path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)
                    || path.endsWith(QLatin1String(".svgz"), Qt::CaseInsensitive);
                if (isSvg) {
                    QSvgRenderer renderer(path);
                    if (renderer.isValid()) {
                        QSize size = renderer.defaultSize();
                        const int maxDim = m_userTextureSvgSizes[i];
                        if (!size.isEmpty()) {
                            size.scale(maxDim, maxDim, Qt::KeepAspectRatio);
                        } else {
                            size = QSize(maxDim, maxDim);
                        }
                        QImage img(size, QImage::Format_RGBA8888);
                        img.fill(Qt::transparent);
                        QPainter painter(&img);
                        renderer.render(&painter);
                        painter.end();
                        m_userTextureImages[i] = img;
                    } else {
                        m_userTextureImages[i] = QImage();
                    }
                } else {
                    m_userTextureImages[i] = QImage(path).convertToFormat(QImage::Format_RGBA8888);
                }
            } else {
                m_userTextureImages[i] = QImage();
            }
        }

        const QString wrapKey = QStringLiteral("uTexture%1_wrap").arg(i);
        if (params.contains(wrapKey)) {
            m_userTextureWraps[i] = params.value(wrapKey).toString();
        }
    }

    update();
}

} // namespace PlasmaZones
