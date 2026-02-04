// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneshaderitem.h"
#include "zoneshadernodebase.h"
#include "zoneshadernoderhi.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QVariantMap>

#include "../../core/logging.h"

namespace PlasmaZones {

// ============================================================================
// Construction / Destruction
// ============================================================================

ZoneShaderItem::ZoneShaderItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    // Enable custom rendering via updatePaintNode
    setFlag(ItemHasContents, true);
}

ZoneShaderItem::~ZoneShaderItem() = default;

// ============================================================================
// Property Setters (with change detection and update() calls)
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

void ZoneShaderItem::setShaderParams(const QVariantMap& params)
{
    if (m_shaderParams == params) {
        return;
    }
    m_shaderParams = params;

    // Extract values from uniform names and apply to individual properties
    // Float params: customParams1_x through customParams4_w (slots 0-15)
    QVector4D newParams1 = m_customParams1;
    QVector4D newParams2 = m_customParams2;
    QVector4D newParams3 = m_customParams3;
    QVector4D newParams4 = m_customParams4;

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

    // Apply float params (will emit signals and call update if changed)
    setCustomParams1(newParams1);
    setCustomParams2(newParams2);
    setCustomParams3(newParams3);
    setCustomParams4(newParams4);

    // Color params: customColor1 through customColor4 (color slots 0-3)
    auto extractColor = [&params](const QString& key, const QVector4D& defaultVal) -> QVector4D {
        if (params.contains(key)) {
            QVariant val = params.value(key);
            if (val.canConvert<QColor>()) {
                QColor color = val.value<QColor>();
                return QVector4D(static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
                                 static_cast<float>(color.blueF()), static_cast<float>(color.alphaF()));
            } else if (val.typeId() == QMetaType::QString) {
                QColor color(val.toString());
                if (color.isValid()) {
                    return QVector4D(static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
                                     static_cast<float>(color.blueF()), static_cast<float>(color.alphaF()));
                }
            }
        }
        return defaultVal;
    };

    setCustomColor1(extractColor(QStringLiteral("customColor1"), m_customColor1));
    setCustomColor2(extractColor(QStringLiteral("customColor2"), m_customColor2));
    setCustomColor3(extractColor(QStringLiteral("customColor3"), m_customColor3));
    setCustomColor4(extractColor(QStringLiteral("customColor4"), m_customColor4));
    setCustomColor5(extractColor(QStringLiteral("customColor5"), m_customColor5));
    setCustomColor6(extractColor(QStringLiteral("customColor6"), m_customColor6));
    setCustomColor7(extractColor(QStringLiteral("customColor7"), m_customColor7));
    setCustomColor8(extractColor(QStringLiteral("customColor8"), m_customColor8));

    Q_EMIT shaderParamsChanged();
    update();
}

void ZoneShaderItem::setCustomParams1(const QVector4D& params)
{
    if (m_customParams1 == params) {
        return;
    }
    m_customParams1 = params;
    Q_EMIT customParamsChanged();
    update();
}

void ZoneShaderItem::setCustomParams2(const QVector4D& params)
{
    if (m_customParams2 == params) {
        return;
    }
    m_customParams2 = params;
    Q_EMIT customParamsChanged();
    update();
}

void ZoneShaderItem::setCustomColor1(const QVector4D& color)
{
    if (m_customColor1 == color) {
        return;
    }
    m_customColor1 = color;
    Q_EMIT customColorsChanged();
    update();
}

void ZoneShaderItem::setCustomColor2(const QVector4D& color)
{
    if (m_customColor2 == color) {
        return;
    }
    m_customColor2 = color;
    Q_EMIT customColorsChanged();
    update();
}

void ZoneShaderItem::setCustomParams3(const QVector4D& params)
{
    if (m_customParams3 == params) {
        return;
    }
    m_customParams3 = params;
    Q_EMIT customParamsChanged();
    update();
}

void ZoneShaderItem::setCustomParams4(const QVector4D& params)
{
    if (m_customParams4 == params) {
        return;
    }
    m_customParams4 = params;
    Q_EMIT customParamsChanged();
    update();
}

void ZoneShaderItem::setCustomColor3(const QVector4D& color)
{
    if (m_customColor3 == color) {
        return;
    }
    m_customColor3 = color;
    Q_EMIT customColorsChanged();
    update();
}

void ZoneShaderItem::setCustomColor4(const QVector4D& color)
{
    if (m_customColor4 == color) {
        return;
    }
    m_customColor4 = color;
    Q_EMIT customColorsChanged();
    update();
}

void ZoneShaderItem::setCustomColor5(const QVector4D& color)
{
    if (m_customColor5 == color) {
        return;
    }
    m_customColor5 = color;
    Q_EMIT customColorsChanged();
    update();
}

void ZoneShaderItem::setCustomColor6(const QVector4D& color)
{
    if (m_customColor6 == color) {
        return;
    }
    m_customColor6 = color;
    Q_EMIT customColorsChanged();
    update();
}

void ZoneShaderItem::setCustomColor7(const QVector4D& color)
{
    if (m_customColor7 == color) {
        return;
    }
    m_customColor7 = color;
    Q_EMIT customColorsChanged();
    update();
}

void ZoneShaderItem::setCustomColor8(const QVector4D& color)
{
    if (m_customColor8 == color) {
        return;
    }
    m_customColor8 = color;
    Q_EMIT customColorsChanged();
    update();
}

// ============================================================================
// Zone Data Parsing
// ============================================================================

void ZoneShaderItem::parseZoneData()
{
    // Get current resolution for normalization
    const float resW = m_iResolution.width() > 0 ? static_cast<float>(m_iResolution.width()) : 1.0f;
    const float resH = m_iResolution.height() > 0 ? static_cast<float>(m_iResolution.height()) : 1.0f;

    // Prepare new zone data structures
    QVector<ZoneRect> newRects;
    QVector<ZoneColor> newFillColors;
    QVector<ZoneColor> newBorderColors;
    newRects.reserve(m_zones.size());
    newFillColors.reserve(m_zones.size());
    newBorderColors.reserve(m_zones.size());

    int highlightedCount = 0;

    for (const QVariant& zoneVar : std::as_const(m_zones)) {
        const QVariantMap z = zoneVar.toMap();

        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        // Extract zone rectangle
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        ZoneRect rect;

        // Extract pixel coordinates and normalize to 0-1 using iResolution
        const float px = z.value(QStringLiteral("x"), 0).toFloat();
        const float py = z.value(QStringLiteral("y"), 0).toFloat();
        const float pw = z.value(QStringLiteral("width"), 0).toFloat();
        const float ph = z.value(QStringLiteral("height"), 0).toFloat();

        rect.x = px / resW;
        rect.y = py / resH;
        rect.width = pw / resW;
        rect.height = ph / resH;

        // Extract zone number and highlighted state
        rect.zoneNumber = z.value(QStringLiteral("zoneNumber"), 0).toInt();
        rect.highlighted = z.value(QStringLiteral("isHighlighted"), false).toBool();

        // Extract shader border properties (stored in snapshot for thread-safe access)
        rect.borderRadius = z.value(QStringLiteral("shaderBorderRadius"), 8.0f).toFloat();
        rect.borderWidth = z.value(QStringLiteral("shaderBorderWidth"), 2.0f).toFloat();

        if (rect.highlighted) {
            ++highlightedCount;
        }

        newRects.append(rect);

        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        // Extract fill color (premultiplied RGBA, 0-1 range)
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        ZoneColor fillColor;
        fillColor.r = z.value(QStringLiteral("fillR"), 0.0f).toFloat();
        fillColor.g = z.value(QStringLiteral("fillG"), 0.0f).toFloat();
        fillColor.b = z.value(QStringLiteral("fillB"), 0.0f).toFloat();
        fillColor.a = z.value(QStringLiteral("fillA"), 0.0f).toFloat();
        newFillColors.append(fillColor);

        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        // Extract border color (RGBA, 0-1 range)
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        ZoneColor borderColor;
        borderColor.r = z.value(QStringLiteral("borderR"), 1.0f).toFloat();
        borderColor.g = z.value(QStringLiteral("borderG"), 1.0f).toFloat();
        borderColor.b = z.value(QStringLiteral("borderB"), 1.0f).toFloat();
        borderColor.a = z.value(QStringLiteral("borderA"), 1.0f).toFloat();
        newBorderColors.append(borderColor);
    }

    // Update zone counts
    m_zoneCount = newRects.size();
    m_highlightedCount = highlightedCount;

    // Thread-safe update of zone data snapshot
    {
        QMutexLocker lock(&m_zoneDataMutex);
        m_zoneData.rects = std::move(newRects);
        m_zoneData.fillColors = std::move(newFillColors);
        m_zoneData.borderColors = std::move(newBorderColors);
        m_zoneData.zoneCount = m_zoneCount;
        m_zoneData.highlightedCount = m_highlightedCount;
        m_zoneData.version = ++m_dataVersion;
    }

    m_zoneDataDirty = true;
}

// ============================================================================
// Thread-Safe Zone Data Accessors
// ============================================================================

ZoneDataSnapshot ZoneShaderItem::getZoneDataSnapshot() const
{
    QMutexLocker lock(&m_zoneDataMutex);
    return m_zoneData;
}

QVector<ZoneRect> ZoneShaderItem::zoneRects() const
{
    QMutexLocker lock(&m_zoneDataMutex);
    return m_zoneData.rects;
}

QVector<ZoneColor> ZoneShaderItem::zoneFillColors() const
{
    QMutexLocker lock(&m_zoneDataMutex);
    return m_zoneData.fillColors;
}

QVector<ZoneColor> ZoneShaderItem::zoneBorderColors() const
{
    QMutexLocker lock(&m_zoneDataMutex);
    return m_zoneData.borderColors;
}

// ============================================================================
// Scene Graph Integration
// ============================================================================

QSGNode* ZoneShaderItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data)
{
    Q_UNUSED(data)

    if (width() <= 0 || height() <= 0) {
        delete oldNode;
        return nullptr;
    }

    ZoneShaderNodeBase* node = static_cast<ZoneShaderNodeBase*>(oldNode);
    if (!node) {
        node = new ZoneShaderNodeRhi(this);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // Sync shader timing uniforms
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    node->setTime(static_cast<float>(m_iTime));
    node->setTimeDelta(static_cast<float>(m_iTimeDelta));
    node->setFrame(m_iFrame);
    node->setResolution(static_cast<float>(width()), static_cast<float>(height()));
    node->setMousePosition(m_iMouse);

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // Sync custom shader parameters (16 floats in 4 vec4s + 8 colors)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    node->setCustomParams1(m_customParams1);
    node->setCustomParams2(m_customParams2);
    node->setCustomParams3(m_customParams3);
    node->setCustomParams4(m_customParams4);
    node->setCustomColor1(
        QColor::fromRgbF(static_cast<float>(m_customColor1.x()), static_cast<float>(m_customColor1.y()),
                         static_cast<float>(m_customColor1.z()), static_cast<float>(m_customColor1.w())));
    node->setCustomColor2(
        QColor::fromRgbF(static_cast<float>(m_customColor2.x()), static_cast<float>(m_customColor2.y()),
                         static_cast<float>(m_customColor2.z()), static_cast<float>(m_customColor2.w())));
    node->setCustomColor3(
        QColor::fromRgbF(static_cast<float>(m_customColor3.x()), static_cast<float>(m_customColor3.y()),
                         static_cast<float>(m_customColor3.z()), static_cast<float>(m_customColor3.w())));
    node->setCustomColor4(
        QColor::fromRgbF(static_cast<float>(m_customColor4.x()), static_cast<float>(m_customColor4.y()),
                         static_cast<float>(m_customColor4.z()), static_cast<float>(m_customColor4.w())));
    node->setCustomColor5(
        QColor::fromRgbF(static_cast<float>(m_customColor5.x()), static_cast<float>(m_customColor5.y()),
                         static_cast<float>(m_customColor5.z()), static_cast<float>(m_customColor5.w())));
    node->setCustomColor6(
        QColor::fromRgbF(static_cast<float>(m_customColor6.x()), static_cast<float>(m_customColor6.y()),
                         static_cast<float>(m_customColor6.z()), static_cast<float>(m_customColor6.w())));
    node->setCustomColor7(
        QColor::fromRgbF(static_cast<float>(m_customColor7.x()), static_cast<float>(m_customColor7.y()),
                         static_cast<float>(m_customColor7.z()), static_cast<float>(m_customColor7.w())));
    node->setCustomColor8(
        QColor::fromRgbF(static_cast<float>(m_customColor8.x()), static_cast<float>(m_customColor8.y()),
                         static_cast<float>(m_customColor8.z()), static_cast<float>(m_customColor8.w())));

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // Sync shader source FIRST (must compile before zone data can be used)
    // Load when: item's m_shaderDirty, OR node not ready (e.g. after releaseResources)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    const bool needLoad = m_shaderDirty.exchange(false) || (m_shaderSource.isValid() && !m_shaderSource.isEmpty() && !node->isShaderReady());
    if (needLoad) {
        if (m_shaderSource.isValid() && !m_shaderSource.isEmpty()) {
            QString fragPath = m_shaderSource.toLocalFile();
            if (m_shaderSource.scheme() == QLatin1String("qrc")) {
                fragPath = QLatin1Char(':') + m_shaderSource.path();
            }

            // Derive vertex shader path: zone.vert (fallback to legacy zone.vert.glsl for compatibility)
            QString vertPath;
            if (!fragPath.isEmpty()) {
                const QString dir = QFileInfo(fragPath).absolutePath();
                const QString vertDefault = dir + QStringLiteral("/zone.vert");
                const QString vertLegacy = dir + QStringLiteral("/zone.vert.glsl");
                vertPath = QFile::exists(vertDefault) ? vertDefault : vertLegacy;
            }

            // Clear old shader sources before loading new ones
            // This prevents stale vertex shader from being used with new fragment shader
            node->setVertexShaderSource(QString());
            node->setFragmentShaderSource(QString());

            bool loaded = true;
            // Load vertex shader first - REQUIRED for zone rendering
            if (!vertPath.isEmpty() && QFile::exists(vertPath)) {
                if (!node->loadVertexShader(vertPath)) {
                    qCWarning(PlasmaZones::lcOverlay) << "Failed to load vertex shader:" << vertPath;
                    loaded = false;
                }
            } else {
                // Vertex shader is required - fail if not found
                if (vertPath.isEmpty()) {
                    qCWarning(PlasmaZones::lcOverlay) << "Required vertex shader not found (cannot derive path - fragment path is empty)";
                } else {
                    const QString dir = QFileInfo(fragPath).absolutePath();
                    qCWarning(PlasmaZones::lcOverlay) << "Required vertex shader not found: expected zone.vert or zone.vert.glsl in" << dir;
                }
                loaded = false;
            }

            // Load fragment shader
            if (loaded && !fragPath.isEmpty()) {
                if (!node->loadFragmentShader(fragPath)) {
                    loaded = false;
                }
            }

            if (loaded) {
                node->invalidateShader(); // Ensure node re-bakes
                setStatus(Status::Ready);
                // Force zone data resync when shader changes successfully
                m_zoneDataDirty = true;
            } else {
                QString errorMsg = node->shaderError();
                if (errorMsg.isEmpty()) {
                    errorMsg = QStringLiteral("Shader loading failed - missing required files");
                }
                setError(errorMsg);
            }
        } else {
            // Source empty (e.g. user selected "none") – clear node so we don't keep drawing old shader
            node->setVertexShaderSource(QString());
            node->setFragmentShaderSource(QString());
            node->invalidateShader();
            setStatus(Status::Null);
        }
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // Sync zone data to the node AFTER shader is ready (thread-safe copy)
    // Only sync if shader is ready, otherwise zone data won't render anyway
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    if (m_zoneDataDirty.load()) {
        // Only sync if shader is ready to avoid pushing data to invalid shader
        if (node->isShaderReady()) {
            m_zoneDataDirty.exchange(false);
            ZoneDataSnapshot snapshot = getZoneDataSnapshot();

            // Convert snapshot to ZoneData format expected by the node
            QVector<ZoneData> zoneDataVec;
            zoneDataVec.reserve(snapshot.zoneCount);

            for (int i = 0; i < snapshot.zoneCount; ++i) {
                ZoneData zd;

                // Rectangle (already normalized 0-1)
                const ZoneRect& rect = snapshot.rects[i];
                zd.rect = QRectF(static_cast<qreal>(rect.x), static_cast<qreal>(rect.y), static_cast<qreal>(rect.width),
                                 static_cast<qreal>(rect.height));
                zd.zoneNumber = rect.zoneNumber;
                zd.isHighlighted = rect.highlighted;

                // Border properties from thread-safe snapshot (no m_zones access needed)
                zd.borderRadius = rect.borderRadius;
                zd.borderWidth = rect.borderWidth;

                // Fill color
                const ZoneColor& fill = snapshot.fillColors[i];
                zd.fillColor = QColor::fromRgbF(static_cast<float>(fill.r), static_cast<float>(fill.g),
                                                static_cast<float>(fill.b), static_cast<float>(fill.a));

                // Border color
                const ZoneColor& border = snapshot.borderColors[i];
                zd.borderColor = QColor::fromRgbF(static_cast<float>(border.r), static_cast<float>(border.g),
                                                  static_cast<float>(border.b), static_cast<float>(border.a));

                zoneDataVec.append(zd);
            }

            node->setZones(zoneDataVec);
            // Note: Per-sync debug logging removed to avoid log spam.
            // Enable via QT_LOGGING_RULES="PlasmaZones.Overlay.debug=true" if needed.
        }
        // If shader not ready, leave dirty flag set so we sync on next frame
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // Update status based on shader node state
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    if (node->isShaderReady() && m_status != Status::Ready) {
        setStatus(Status::Ready);
    } else if (!node->shaderError().isEmpty() && m_status != Status::Error) {
        setError(node->shaderError());
    }

    // Mark node as dirty to trigger re-render
    node->markDirty(QSGNode::DirtyMaterial);

    return node;
}

// ============================================================================
// Geometry Handling
// ============================================================================

void ZoneShaderItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);

    // Update iResolution when geometry changes
    if (newGeometry.size() != oldGeometry.size()) {
        const QSizeF newSize = newGeometry.size();

        if (m_iResolution != newSize) {
            m_iResolution = newSize;
            Q_EMIT iResolutionChanged();

            // Re-parse zones with new resolution for normalization
            if (!m_zones.isEmpty()) {
                parseZoneData();
            }
        }

        update();
    }
}

void ZoneShaderItem::componentComplete()
{
    QQuickItem::componentComplete();

    // Initialize resolution from item size if not set
    if (m_iResolution.isEmpty() && width() > 0 && height() > 0) {
        m_iResolution = QSizeF(width(), height());
        Q_EMIT iResolutionChanged();
    }

    // Parse initial zone data if any
    if (!m_zones.isEmpty()) {
        parseZoneData();
    }

    // Load shader if source is set
    if (m_shaderSource.isValid() && !m_shaderSource.isEmpty()) {
        loadShader();
    }
}

// ============================================================================
// Shader Loading
// ============================================================================

void ZoneShaderItem::loadShader()
{
    if (!m_shaderSource.isValid() || m_shaderSource.isEmpty()) {
        setStatus(Status::Null);
        return;
    }

    setStatus(Status::Loading);
    m_shaderDirty = true;
    update();
}

// ============================================================================
// Status Management
// ============================================================================

void ZoneShaderItem::setError(const QString& error)
{
    if (m_errorLog != error) {
        m_errorLog = error;
        Q_EMIT errorLogChanged();
    }
    setStatus(Status::Error);
}

void ZoneShaderItem::setStatus(Status status)
{
    if (m_status != status) {
        m_status = status;
        Q_EMIT statusChanged();
    }
}

} // namespace PlasmaZones
