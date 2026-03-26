// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneshaderitem.h"
#include "zoneshadernodebase.h"
#include "zoneshadernoderhi.h"

#include "../../core/constants.h"
#include "../../core/logging.h"

#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QVariantMap>

namespace PlasmaZones {

// DRY helper: convert QVector4D color to QColor (eliminates 8 repeated 3-line blocks in updatePaintNode)
static QColor vec4ToQColor(const QVector4D& v)
{
    return QColor::fromRgbF(static_cast<float>(v.x()), static_cast<float>(v.y()), static_cast<float>(v.z()),
                            static_cast<float>(v.w()));
}

// ============================================================================
// Construction / Destruction
// ============================================================================

ZoneShaderItem::ZoneShaderItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    // Enable custom rendering via updatePaintNode
    setFlag(ItemHasContents, true);
}

ZoneShaderItem::~ZoneShaderItem()
{
    // Invalidate the render node's back-pointer to this item BEFORE our QObject
    // members are torn down.  The scene graph render thread may still call
    // prepare()/render() on the node between now and the node's eventual
    // deletion; without this, m_item would be a dangling pointer.
    if (m_renderNode) {
        m_renderNode->invalidateItem();
    }
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
    int index = 0;

    for (const QVariant& zoneVar : std::as_const(m_zones)) {
        const QVariantMap z = zoneVar.toMap();

        // Extract zone rectangle
        ZoneRect rect;

        // Extract pixel coordinates and normalize to 0-1 using iResolution
        const float px = z.value(QLatin1String(JsonKeys::X), 0).toFloat();
        const float py = z.value(QLatin1String(JsonKeys::Y), 0).toFloat();
        const float pw = z.value(QLatin1String(JsonKeys::Width), 0).toFloat();
        const float ph = z.value(QLatin1String(JsonKeys::Height), 0).toFloat();

        rect.x = px / resW;
        rect.y = py / resH;
        rect.width = pw / resW;
        rect.height = ph / resH;

        // Extract zone number and highlighted state (zone selector or hover override)
        rect.zoneNumber = z.value(QLatin1String(JsonKeys::ZoneNumber), 0).toInt();
        rect.highlighted = z.value(QLatin1String(JsonKeys::IsHighlighted), false).toBool()
            || (m_hoveredZoneIndex >= 0 && index == m_hoveredZoneIndex);

        // Extract shader border properties (stored in snapshot for thread-safe access)
        rect.borderRadius = z.value(QLatin1String("shaderBorderRadius"), 8.0f).toFloat();
        rect.borderWidth = z.value(QLatin1String("shaderBorderWidth"), 2.0f).toFloat();

        if (rect.highlighted) {
            ++highlightedCount;
        }

        newRects.append(rect);

        // Extract fill color (premultiplied RGBA, 0-1 range)
        ZoneColor fillColor;
        fillColor.r = z.value(QLatin1String("fillR"), 0.0f).toFloat();
        fillColor.g = z.value(QLatin1String("fillG"), 0.0f).toFloat();
        fillColor.b = z.value(QLatin1String("fillB"), 0.0f).toFloat();
        fillColor.a = z.value(QLatin1String("fillA"), 0.0f).toFloat();
        newFillColors.append(fillColor);

        // Extract border color (RGBA, 0-1 range)
        ZoneColor borderColor;
        borderColor.r = z.value(QLatin1String("borderR"), 1.0f).toFloat();
        borderColor.g = z.value(QLatin1String("borderG"), 1.0f).toFloat();
        borderColor.b = z.value(QLatin1String("borderB"), 1.0f).toFloat();
        borderColor.a = z.value(QLatin1String("borderA"), 1.0f).toFloat();
        newBorderColors.append(borderColor);
        ++index;
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

void ZoneShaderItem::updateHoveredHighlightOnly()
{
    // Precondition: m_zoneData must be populated by a prior setZones/parseZoneData call.
    if (m_zoneData.rects.size() != static_cast<qsizetype>(m_zones.size())) {
        qWarning(lcOverlay) << "updateHoveredHighlightOnly: zone data out of sync (rects=" << m_zoneData.rects.size()
                            << "zones=" << m_zones.size() << ") - setZones must be called first";
        return;
    }
    int highlightedCount = 0;
    {
        QMutexLocker lock(&m_zoneDataMutex);
        for (int i = 0; i < m_zoneData.rects.size(); ++i) {
            const bool fromZone = (i < m_zones.size())
                ? m_zones[i].toMap().value(QLatin1String(JsonKeys::IsHighlighted), false).toBool()
                : false;
            const bool hovered = (m_hoveredZoneIndex >= 0 && i == m_hoveredZoneIndex);
            m_zoneData.rects[i].highlighted = fromZone || hovered;
            if (m_zoneData.rects[i].highlighted) {
                ++highlightedCount;
            }
        }
        m_zoneData.highlightedCount = highlightedCount;
        m_zoneData.version = ++m_dataVersion;
    }
    const int oldCount = m_highlightedCount;
    m_highlightedCount = highlightedCount;
    if (oldCount != m_highlightedCount) {
        Q_EMIT highlightedCountChanged();
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
        if (oldNode) {
            // Invalidate the old node's back-pointer before deleting it
            if (m_renderNode) {
                m_renderNode->invalidateItem();
                m_renderNode = nullptr;
            }
            delete oldNode;
        }
        return nullptr;
    }

    ZoneShaderNodeBase* node = static_cast<ZoneShaderNodeBase*>(oldNode);
    if (!node) {
        // Scene graph deleted the previous node (e.g. releaseResources), or first call.
        // Clear stale pointer — the old node no longer exists.
        m_renderNode = nullptr;
        auto* rhiNode = new ZoneShaderNodeRhi(this);
        m_renderNode = rhiNode;
        node = rhiNode;
    }

    // Sync shader timing uniforms
    node->setTime(static_cast<float>(m_iTime));
    node->setTimeDelta(static_cast<float>(m_iTimeDelta));
    node->setFrame(m_iFrame);
    node->setResolution(static_cast<float>(width()), static_cast<float>(height()));
    node->setMousePosition(m_iMouse);

    // Sync custom shader parameters (32 floats in 8 vec4s + 16 colors)
    node->setCustomParams1(m_customParams1);
    node->setCustomParams2(m_customParams2);
    node->setCustomParams3(m_customParams3);
    node->setCustomParams4(m_customParams4);
    node->setCustomParams5(m_customParams5);
    node->setCustomParams6(m_customParams6);
    node->setCustomParams7(m_customParams7);
    node->setCustomParams8(m_customParams8);
    node->setCustomColor1(vec4ToQColor(m_customColor1));
    node->setCustomColor2(vec4ToQColor(m_customColor2));
    node->setCustomColor3(vec4ToQColor(m_customColor3));
    node->setCustomColor4(vec4ToQColor(m_customColor4));
    node->setCustomColor5(vec4ToQColor(m_customColor5));
    node->setCustomColor6(vec4ToQColor(m_customColor6));
    node->setCustomColor7(vec4ToQColor(m_customColor7));
    node->setCustomColor8(vec4ToQColor(m_customColor8));
    node->setCustomColor9(vec4ToQColor(m_customColor9));
    node->setCustomColor10(vec4ToQColor(m_customColor10));
    node->setCustomColor11(vec4ToQColor(m_customColor11));
    node->setCustomColor12(vec4ToQColor(m_customColor12));
    node->setCustomColor13(vec4ToQColor(m_customColor13));
    node->setCustomColor14(vec4ToQColor(m_customColor14));
    node->setCustomColor15(vec4ToQColor(m_customColor15));
    node->setCustomColor16(vec4ToQColor(m_customColor16));

    // Sync labels texture (pre-rendered zone numbers for shader pass)
    {
        QMutexLocker lock(&m_labelsTextureMutex);
        node->setLabelsTexture(m_labelsTexture);
    }

    // Sync audio spectrum (CAVA bar data for audio-reactive shaders)
    node->setAudioSpectrum(m_audioSpectrum);

    // Sync user textures (bindings 7-10)
    for (int i = 0; i < 4; ++i) {
        node->setUserTexture(i, m_userTextureImages[i]);
        node->setUserTextureWrap(i, m_userTextureWraps[i]);
    }

    // Sync desktop wallpaper texture (binding 11)
    node->setUseWallpaper(m_useWallpaper);
    {
        QMutexLocker lock(&m_wallpaperTextureMutex);
        node->setWallpaperTexture(m_wallpaperTexture);
    }

    // Sync buffer shader path (multipass)
    QStringList effectivePaths = m_bufferShaderPaths;
    if (effectivePaths.isEmpty() && !m_bufferShaderPath.isEmpty()) {
        effectivePaths.append(m_bufferShaderPath);
    }
    while (effectivePaths.size() > 4) {
        effectivePaths.removeLast();
    }
    node->setBufferShaderPaths(effectivePaths);
    node->setBufferFeedback(m_bufferFeedback);
    node->setBufferScale(m_bufferScale);
    node->setBufferWrap(m_bufferWrap);

    // Sync shader source FIRST (must compile before zone data can be used)
    // Load when: item's m_shaderDirty, OR node not ready (e.g. after releaseResources)
    const bool needLoad = m_shaderDirty.exchange(false)
        || (m_shaderSource.isValid() && !m_shaderSource.isEmpty() && !node->isShaderReady());
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
                    qCWarning(PlasmaZones::lcOverlay)
                        << "Required vertex shader not found (cannot derive path - fragment path is empty)";
                } else {
                    const QString dir = QFileInfo(fragPath).absolutePath();
                    qCWarning(PlasmaZones::lcOverlay)
                        << "Required vertex shader not found: expected zone.vert or zone.vert.glsl in" << dir;
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

    // Sync zone data to the node AFTER shader is ready (thread-safe copy)
    // Only sync if shader is ready, otherwise zone data won't render anyway
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
        }
        // If shader not ready, leave dirty flag set so we sync on next frame
    }

    // Update status based on shader node state
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
