// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../zoneshaderitem.h"
#include "../rendering_macros.h"

#include "../../../core/constants.h"
#include "../../../core/logging.h"

#include <QFile>
#include <QMutexLocker>

namespace PlasmaZones {

// ============================================================================
// Animation / Resolution / Mouse Setters
// ============================================================================

void ZoneShaderItem::setITime(qreal time)
{
    // Use explicit epsilon comparison - qFuzzyCompare fails when comparing with zero
    constexpr qreal epsilon = 1e-9;
    if (qAbs(m_iTime - time) < epsilon) {
        return;
    }
    m_iTime = time;
    Q_EMIT iTimeChanged();
    update();
}

void ZoneShaderItem::setITimeDelta(qreal delta)
{
    // Use explicit epsilon comparison - qFuzzyCompare fails when comparing with zero
    constexpr qreal epsilon = 1e-9;
    if (qAbs(m_iTimeDelta - delta) < epsilon) {
        return;
    }
    m_iTimeDelta = delta;
    Q_EMIT iTimeDeltaChanged();
    update();
}

void ZoneShaderItem::setIFrame(int frame)
{
    if (m_iFrame == frame) {
        return;
    }
    m_iFrame = frame;
    Q_EMIT iFrameChanged();
    update();
}

void ZoneShaderItem::setIResolution(const QSizeF& resolution)
{
    if (m_iResolution == resolution) {
        return;
    }
    m_iResolution = resolution;
    Q_EMIT iResolutionChanged();
    update();
}

void ZoneShaderItem::setIMouse(const QPointF& mouse)
{
    if (m_iMouse == mouse) {
        return;
    }
    m_iMouse = mouse;
    Q_EMIT iMouseChanged();
    update();
}

// ============================================================================
// Zone Data Setters
// ============================================================================

void ZoneShaderItem::setZones(const QVariantList& zones)
{
    // Note: We intentionally skip deep comparison here for performance reasons.
    // Zone data changes frequently during animations, and comparing large QVariantLists
    // on every frame would be more expensive than re-parsing. The parseZoneData()
    // function handles the actual change detection via m_zoneDataDirty flag.
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
        updateHoveredHighlightOnly();  // Lightweight: only update highlight flags, avoids full parse/sync
    }
    Q_EMIT hoveredZoneIndexChanged();
    update();
}

// ============================================================================
// Shader Source / Buffer Setters
// ============================================================================

void ZoneShaderItem::setShaderSource(const QUrl& source)
{
    if (m_shaderSource == source) {
        return;
    }
    m_shaderSource = source;
    m_shaderDirty = true;
    setStatus(Status::Loading);
    Q_EMIT shaderSourceChanged();
    update();
}

void ZoneShaderItem::setBufferShaderPath(const QString& path)
{
    if (m_bufferShaderPath == path) {
        return;
    }
    m_bufferShaderPath = path;
    const QStringList newPaths = path.isEmpty() ? QStringList() : QStringList{path};
    if (m_bufferShaderPaths != newPaths) {
        m_bufferShaderPaths = newPaths;
        Q_EMIT bufferShaderPathsChanged();
    }
    m_shaderDirty = true;
    update();
    Q_EMIT bufferShaderPathChanged();
}

void ZoneShaderItem::setBufferShaderPaths(const QStringList& paths)
{
    if (m_bufferShaderPaths == paths) {
        return;
    }
    m_bufferShaderPaths = paths;
    const QString newPath = paths.isEmpty() ? QString() : paths.constFirst();
    if (m_bufferShaderPath != newPath) {
        m_bufferShaderPath = newPath;
        Q_EMIT bufferShaderPathChanged();
    }
    m_shaderDirty = true;
    update();
    Q_EMIT bufferShaderPathsChanged();
}

void ZoneShaderItem::setBufferFeedback(bool enable)
{
    if (m_bufferFeedback == enable) {
        return;
    }
    m_bufferFeedback = enable;
    Q_EMIT bufferFeedbackChanged();
    update();
}

void ZoneShaderItem::setBufferScale(qreal scale)
{
    const qreal clamped = qBound(0.125, scale, 1.0);
    if (qFuzzyCompare(m_bufferScale, clamped)) {
        return;
    }
    m_bufferScale = clamped;
    Q_EMIT bufferScaleChanged();
    update();
}

void ZoneShaderItem::setBufferWrap(const QString& wrap)
{
    const QString use = (wrap == QLatin1String("repeat")) ? QStringLiteral("repeat") : QStringLiteral("clamp");
    if (m_bufferWrap == use) {
        return;
    }
    m_bufferWrap = use;
    Q_EMIT bufferWrapChanged();
    update();
}

// ============================================================================
// Shader Params (complex logic — kept verbatim, not macro-able)
// ============================================================================

void ZoneShaderItem::setShaderParams(const QVariantMap& params)
{
    if (m_shaderParams == params) {
        return;
    }
    m_shaderParams = params;

    // Extract values from uniform names and apply to individual properties
    // Float params: customParams1_x through customParams8_w (slots 0-31)
    QVector4D newParams1 = m_customParams1;
    QVector4D newParams2 = m_customParams2;
    QVector4D newParams3 = m_customParams3;
    QVector4D newParams4 = m_customParams4;
    QVector4D newParams5 = m_customParams5;
    QVector4D newParams6 = m_customParams6;
    QVector4D newParams7 = m_customParams7;
    QVector4D newParams8 = m_customParams8;

    auto extractFloat = [&params](const QString& key, float defaultVal) -> float {
        if (params.contains(key)) {
            bool ok = false;
            float val = params.value(key).toFloat(&ok);
            return ok ? val : defaultVal;
        }
        return defaultVal;
    };

    // customParams1 (slots 0-3)
    newParams1.setX(extractFloat(QStringLiteral("customParams1_x"), newParams1.x()));
    newParams1.setY(extractFloat(QStringLiteral("customParams1_y"), newParams1.y()));
    newParams1.setZ(extractFloat(QStringLiteral("customParams1_z"), newParams1.z()));
    newParams1.setW(extractFloat(QStringLiteral("customParams1_w"), newParams1.w()));

    // customParams2 (slots 4-7)
    newParams2.setX(extractFloat(QStringLiteral("customParams2_x"), newParams2.x()));
    newParams2.setY(extractFloat(QStringLiteral("customParams2_y"), newParams2.y()));
    newParams2.setZ(extractFloat(QStringLiteral("customParams2_z"), newParams2.z()));
    newParams2.setW(extractFloat(QStringLiteral("customParams2_w"), newParams2.w()));

    // customParams3 (slots 8-11)
    newParams3.setX(extractFloat(QStringLiteral("customParams3_x"), newParams3.x()));
    newParams3.setY(extractFloat(QStringLiteral("customParams3_y"), newParams3.y()));
    newParams3.setZ(extractFloat(QStringLiteral("customParams3_z"), newParams3.z()));
    newParams3.setW(extractFloat(QStringLiteral("customParams3_w"), newParams3.w()));

    // customParams4 (slots 12-15)
    newParams4.setX(extractFloat(QStringLiteral("customParams4_x"), newParams4.x()));
    newParams4.setY(extractFloat(QStringLiteral("customParams4_y"), newParams4.y()));
    newParams4.setZ(extractFloat(QStringLiteral("customParams4_z"), newParams4.z()));
    newParams4.setW(extractFloat(QStringLiteral("customParams4_w"), newParams4.w()));

    // customParams5 (slots 16-19)
    newParams5.setX(extractFloat(QStringLiteral("customParams5_x"), newParams5.x()));
    newParams5.setY(extractFloat(QStringLiteral("customParams5_y"), newParams5.y()));
    newParams5.setZ(extractFloat(QStringLiteral("customParams5_z"), newParams5.z()));
    newParams5.setW(extractFloat(QStringLiteral("customParams5_w"), newParams5.w()));

    // customParams6 (slots 20-23)
    newParams6.setX(extractFloat(QStringLiteral("customParams6_x"), newParams6.x()));
    newParams6.setY(extractFloat(QStringLiteral("customParams6_y"), newParams6.y()));
    newParams6.setZ(extractFloat(QStringLiteral("customParams6_z"), newParams6.z()));
    newParams6.setW(extractFloat(QStringLiteral("customParams6_w"), newParams6.w()));

    // customParams7 (slots 24-27)
    newParams7.setX(extractFloat(QStringLiteral("customParams7_x"), newParams7.x()));
    newParams7.setY(extractFloat(QStringLiteral("customParams7_y"), newParams7.y()));
    newParams7.setZ(extractFloat(QStringLiteral("customParams7_z"), newParams7.z()));
    newParams7.setW(extractFloat(QStringLiteral("customParams7_w"), newParams7.w()));

    // customParams8 (slots 28-31)
    newParams8.setX(extractFloat(QStringLiteral("customParams8_x"), newParams8.x()));
    newParams8.setY(extractFloat(QStringLiteral("customParams8_y"), newParams8.y()));
    newParams8.setZ(extractFloat(QStringLiteral("customParams8_z"), newParams8.z()));
    newParams8.setW(extractFloat(QStringLiteral("customParams8_w"), newParams8.w()));

    // Apply float params (will emit signals and call update if changed)
    setCustomParams1(newParams1);
    setCustomParams2(newParams2);
    setCustomParams3(newParams3);
    setCustomParams4(newParams4);
    setCustomParams5(newParams5);
    setCustomParams6(newParams6);
    setCustomParams7(newParams7);
    setCustomParams8(newParams8);

    // Color params: customColor1–16 (color slots 0–15), single helper + loop
    auto extractColor = [&params](const QString& key, const QVector4D& defaultVal) -> QVector4D {
        if (params.contains(key)) {
            QVariant val = params.value(key);
            if (val.canConvert<QColor>()) {
                QColor color = val.value<QColor>();
                return QVector4D(static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
                                 static_cast<float>(color.blueF()), static_cast<float>(color.alphaF()));
            }
            if (val.typeId() == QMetaType::QString) {
                QColor color(val.toString());
                if (color.isValid()) {
                    return QVector4D(static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
                                     static_cast<float>(color.blueF()), static_cast<float>(color.alphaF()));
                }
            }
        }
        return defaultVal;
    };

    static const char* const colorKeys[] = {
        "customColor1", "customColor2", "customColor3", "customColor4",
        "customColor5", "customColor6", "customColor7", "customColor8",
        "customColor9", "customColor10", "customColor11", "customColor12",
        "customColor13", "customColor14", "customColor15", "customColor16",
    };
    for (int i = 0; i < 16; ++i) {
        setCustomColorByIndex(i + 1, extractColor(QString::fromLatin1(colorKeys[i]), customColorByIndex(i + 1)));
    }

    // User texture params: uTexture0-3 paths and uTexture%1_wrap values
    for (int i = 0; i < 4; ++i) {
        const QString texKey = QStringLiteral("uTexture%1").arg(i);
        if (params.contains(texKey)) {
            const QString path = params.value(texKey).toString();
            if (m_userTexturePaths[i] != path) {
                m_userTexturePaths[i] = path;
                if (!path.isEmpty() && QFile::exists(path)) {
                    m_userTextureImages[i] = QImage(path).convertToFormat(QImage::Format_RGBA8888);
                } else {
                    m_userTextureImages[i] = QImage();
                }
            }
        }
        const QString wrapKey = QStringLiteral("uTexture%1_wrap").arg(i);
        if (params.contains(wrapKey)) {
            m_userTextureWraps[i] = params.value(wrapKey).toString();
        }
    }

    Q_EMIT shaderParamsChanged();
    update();
}

// ============================================================================
// Custom Params (DRY macro — 4 invocations replace 59 lines)
// ============================================================================

SHADERITEM_VEC4_SETTER(CustomParams1, m_customParams1, customParamsChanged)
SHADERITEM_VEC4_SETTER(CustomParams2, m_customParams2, customParamsChanged)
SHADERITEM_VEC4_SETTER(CustomParams3, m_customParams3, customParamsChanged)
SHADERITEM_VEC4_SETTER(CustomParams4, m_customParams4, customParamsChanged)
SHADERITEM_VEC4_SETTER(CustomParams5, m_customParams5, customParamsChanged)
SHADERITEM_VEC4_SETTER(CustomParams6, m_customParams6, customParamsChanged)
SHADERITEM_VEC4_SETTER(CustomParams7, m_customParams7, customParamsChanged)
SHADERITEM_VEC4_SETTER(CustomParams8, m_customParams8, customParamsChanged)

// ============================================================================
// Custom Colors (DRY macro — 8 invocations replace 99 lines)
// ============================================================================

SHADERITEM_VEC4_SETTER(CustomColor1, m_customColor1, customColorsChanged)
SHADERITEM_VEC4_SETTER(CustomColor2, m_customColor2, customColorsChanged)
SHADERITEM_VEC4_SETTER(CustomColor3, m_customColor3, customColorsChanged)
SHADERITEM_VEC4_SETTER(CustomColor4, m_customColor4, customColorsChanged)
SHADERITEM_VEC4_SETTER(CustomColor5, m_customColor5, customColorsChanged)
SHADERITEM_VEC4_SETTER(CustomColor6, m_customColor6, customColorsChanged)
SHADERITEM_VEC4_SETTER(CustomColor7, m_customColor7, customColorsChanged)
SHADERITEM_VEC4_SETTER(CustomColor8, m_customColor8, customColorsChanged)
SHADERITEM_VEC4_SETTER(CustomColor9, m_customColor9, customColorsChanged)
SHADERITEM_VEC4_SETTER(CustomColor10, m_customColor10, customColorsChanged)
SHADERITEM_VEC4_SETTER(CustomColor11, m_customColor11, customColorsChanged)
SHADERITEM_VEC4_SETTER(CustomColor12, m_customColor12, customColorsChanged)
SHADERITEM_VEC4_SETTER(CustomColor13, m_customColor13, customColorsChanged)
SHADERITEM_VEC4_SETTER(CustomColor14, m_customColor14, customColorsChanged)
SHADERITEM_VEC4_SETTER(CustomColor15, m_customColor15, customColorsChanged)
SHADERITEM_VEC4_SETTER(CustomColor16, m_customColor16, customColorsChanged)

// ============================================================================
// Labels Texture / Audio Spectrum
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
        if (m_labelsTexture.size() == newImage.size()) {
            const int pixels = newImage.width() * newImage.height();
            if (pixels <= 512 * 512 && m_labelsTexture == newImage) {
                return;
            }
        }
        m_labelsTexture = std::move(newImage);
    }
    Q_EMIT labelsTextureChanged();
    update();
}

QVariant ZoneShaderItem::audioSpectrumVariant() const
{
    return QVariant::fromValue(m_audioSpectrum);
}

void ZoneShaderItem::setAudioSpectrumVariant(const QVariant& spectrum)
{
    // Fast path: QVector<float> from C++ OverlayService (no per-element conversion)
    if (spectrum.metaType() == QMetaType::fromType<QVector<float>>()) {
        setAudioSpectrumRaw(spectrum.value<QVector<float>>());
        return;
    }
    // Slow path: QVariantList from QML (JS array)
    const QVariantList list = spectrum.toList();
    QVector<float> vec;
    vec.reserve(list.size());
    for (const QVariant& v : list) {
        bool ok = false;
        const float f = v.toFloat(&ok);
        vec.append(ok ? qBound(0.0f, f, 1.0f) : 0.0f);
    }
    if (m_audioSpectrum == vec) {
        return;
    }
    m_audioSpectrum = std::move(vec);
    Q_EMIT audioSpectrumChanged();
    update();
}

void ZoneShaderItem::setAudioSpectrumRaw(const QVector<float>& spectrum)
{
    if (m_audioSpectrum == spectrum) {
        return;
    }
    m_audioSpectrum = spectrum;
    Q_EMIT audioSpectrumChanged();
    update();
}

// ============================================================================
// Custom Color By Index (for setShaderParams loop)
// ============================================================================

QVector4D ZoneShaderItem::customColorByIndex(int index) const
{
    switch (index) {
    case 1: return m_customColor1;
    case 2: return m_customColor2;
    case 3: return m_customColor3;
    case 4: return m_customColor4;
    case 5: return m_customColor5;
    case 6: return m_customColor6;
    case 7: return m_customColor7;
    case 8: return m_customColor8;
    case 9: return m_customColor9;
    case 10: return m_customColor10;
    case 11: return m_customColor11;
    case 12: return m_customColor12;
    case 13: return m_customColor13;
    case 14: return m_customColor14;
    case 15: return m_customColor15;
    case 16: return m_customColor16;
    default: return QVector4D();
    }
}

void ZoneShaderItem::setCustomColorByIndex(int index, const QVector4D& color)
{
    switch (index) {
    case 1: setCustomColor1(color); break;
    case 2: setCustomColor2(color); break;
    case 3: setCustomColor3(color); break;
    case 4: setCustomColor4(color); break;
    case 5: setCustomColor5(color); break;
    case 6: setCustomColor6(color); break;
    case 7: setCustomColor7(color); break;
    case 8: setCustomColor8(color); break;
    case 9: setCustomColor9(color); break;
    case 10: setCustomColor10(color); break;
    case 11: setCustomColor11(color); break;
    case 12: setCustomColor12(color); break;
    case 13: setCustomColor13(color); break;
    case 14: setCustomColor14(color); break;
    case 15: setCustomColor15(color); break;
    case 16: setCustomColor16(color); break;
    default: break;
    }
}

} // namespace PlasmaZones
