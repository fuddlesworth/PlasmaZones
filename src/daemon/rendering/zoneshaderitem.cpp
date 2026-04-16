// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneshaderitem.h"
#include "zoneshadernoderhi.h"

#include "../../core/constants.h"
#include "../../core/logging.h"

#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QVariantMap>

namespace PlasmaZones {

// ============================================================================
// Construction / Destruction
// ============================================================================

ZoneShaderItem::ZoneShaderItem(QQuickItem* parent)
    : PhosphorRendering::ShaderEffect(parent)
{
    // Set PlasmaZones-specific shader include paths so that #include <common.glsl>
    // in zone.vert/effect.frag resolves to the system shaders directory.
    const QString systemShaderDir = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/shaders"), QStandardPaths::LocateDirectory);
    QStringList includePaths;
    if (!systemShaderDir.isEmpty()) {
        includePaths.append(systemShaderDir);
    }
    setShaderIncludePaths(includePaths);
}

ZoneShaderItem::~ZoneShaderItem()
{
    // The parent destructor handles invalidateItem() on the render node.
    // We just need to clear our zone-specific tracking pointer.
    m_zoneRenderNode = nullptr;
}

// ============================================================================
// Zone Data Parsing
// ============================================================================

void ZoneShaderItem::parseZoneData()
{
    // Get current resolution for normalization
    const float resW = iResolution().width() > 0 ? static_cast<float>(iResolution().width()) : 1.0f;
    const float resH = iResolution().height() > 0 ? static_cast<float>(iResolution().height()) : 1.0f;

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
    // Pre-compute highlight flags outside the mutex to avoid blocking the render
    // thread with QVariant::toMap() conversions.
    const int count = static_cast<int>(m_zoneData.rects.size());
    QVector<bool> highlights(count, false);
    int highlightedCount = 0;
    for (int i = 0; i < count; ++i) {
        const bool fromZone = (i < m_zones.size())
            ? m_zones[i].toMap().value(QLatin1String(JsonKeys::IsHighlighted), false).toBool()
            : false;
        const bool hovered = (m_hoveredZoneIndex >= 0 && i == m_hoveredZoneIndex);
        highlights[i] = fromZone || hovered;
        if (highlights[i]) {
            ++highlightedCount;
        }
    }
    {
        QMutexLocker lock(&m_zoneDataMutex);
        for (int i = 0; i < count; ++i) {
            m_zoneData.rects[i].highlighted = highlights[i];
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
            // The parent tracks m_renderNode, but we also track m_zoneRenderNode
            m_zoneRenderNode = nullptr;
            // Let parent handle invalidation of the base render node
            if (auto* rhiNode = static_cast<ZoneShaderNodeRhi*>(oldNode)) {
                rhiNode->invalidateItem();
            }
            delete oldNode;
        }
        return nullptr;
    }

    auto* node = static_cast<ZoneShaderNodeRhi*>(oldNode);
    if (!node) {
        // Scene graph deleted the previous node, or first call.
        m_zoneRenderNode = nullptr;
        node = new ZoneShaderNodeRhi(this);
        m_zoneRenderNode = node;
        qCInfo(PlasmaZones::lcOverlay) << "updatePaintNode: created NEW ZoneShaderNodeRhi (oldNode was null)";
    } else {
        qCDebug(PlasmaZones::lcOverlay) << "updatePaintNode: reusing existing node, shaderReady:"
                                        << node->isShaderReady();
    }

    // ── Sync Shadertoy uniforms (base class properties → node) ──────
    node->setTime(iTime());
    node->setTimeDelta(static_cast<float>(iTimeDelta()));
    node->setFrame(iFrame());
    node->setResolution(static_cast<float>(width()), static_cast<float>(height()));
    node->setMousePosition(iMouse());

    // ── Sync custom parameters (indexed API) ────────────────────────
    node->setCustomParams(0, customParams1());
    node->setCustomParams(1, customParams2());
    node->setCustomParams(2, customParams3());
    node->setCustomParams(3, customParams4());
    node->setCustomParams(4, customParams5());
    node->setCustomParams(5, customParams6());
    node->setCustomParams(6, customParams7());
    node->setCustomParams(7, customParams8());

    // ── Sync custom colors (indexed API) ─────────────────────────────
    node->setCustomColor(0, customColor1());
    node->setCustomColor(1, customColor2());
    node->setCustomColor(2, customColor3());
    node->setCustomColor(3, customColor4());
    node->setCustomColor(4, customColor5());
    node->setCustomColor(5, customColor6());
    node->setCustomColor(6, customColor7());
    node->setCustomColor(7, customColor8());
    node->setCustomColor(8, customColor9());
    node->setCustomColor(9, customColor10());
    node->setCustomColor(10, customColor11());
    node->setCustomColor(11, customColor12());
    node->setCustomColor(12, customColor13());
    node->setCustomColor(13, customColor14());
    node->setCustomColor(14, customColor15());
    node->setCustomColor(15, customColor16());

    // ── Sync labels texture (zone-specific, not in parent) ───────────
    {
        QMutexLocker lock(&m_labelsTextureMutex);
        node->setLabelsTexture(m_labelsTexture);
    }

    // ── Sync audio spectrum ──────────────────────────────────────────
    node->setAudioSpectrum(audioSpectrumVariant().value<QVector<float>>());

    // ── Sync user textures (bindings 7-10) ───────────────────────────
    for (int i = 0; i < 4; ++i) {
        node->setUserTexture(i, m_userTextureImages[i]);
        node->setUserTextureWrap(i, m_userTextureWraps[i]);
    }

    // ── Sync depth buffer and wallpaper ──────────────────────────────
    node->setUseDepthBuffer(useDepthBuffer());
    node->setUseWallpaper(useWallpaper());
    node->setWallpaperTexture(wallpaperTexture());

    // ── Sync multipass buffer configuration ──────────────────────────
    QStringList effectivePaths = bufferShaderPaths();
    if (effectivePaths.isEmpty() && !bufferShaderPath().isEmpty()) {
        effectivePaths.append(bufferShaderPath());
    }
    while (effectivePaths.size() > 4) {
        effectivePaths.removeLast();
    }
    node->setBufferShaderPaths(effectivePaths);
    node->setBufferFeedback(bufferFeedback());
    node->setBufferScale(bufferScale());
    node->setBufferWrap(bufferWrap());
    if (!bufferWraps().isEmpty()) {
        node->setBufferWraps(bufferWraps());
    }
    node->setBufferFilter(bufferFilter());
    if (!bufferFilters().isEmpty()) {
        node->setBufferFilters(bufferFilters());
    }

    // ── Sync uniform extension (zone extension) ──────────────────────
    node->setUniformExtension(uniformExtension());

    // ── Sync shader source ───────────────────────────────────────────
    // PlasmaZones derives vertex shader path from zone.vert in the same directory.
    const bool needLoad =
        !node->isShaderReady() || (shaderSource().isValid() && !shaderSource().isEmpty() && !node->isShaderReady());
    // Check if parent's shader dirty flag was set (parent uses atomic m_shaderDirty)
    // Since we inherit ShaderEffect, we can check status changes
    const bool shaderSourceValid = shaderSource().isValid() && !shaderSource().isEmpty();
    const bool statusIsLoading = (status() == Status::Loading);

    if (statusIsLoading || needLoad) {
        if (shaderSourceValid) {
            QString fragPath = shaderSource().toLocalFile();
            if (shaderSource().scheme() == QLatin1String("qrc")) {
                fragPath = QLatin1Char(':') + shaderSource().path();
            }

            // Derive vertex shader path: zone.vert (fallback to legacy zone.vert.glsl)
            QString vertPath;
            if (!fragPath.isEmpty()) {
                const QString dir = QFileInfo(fragPath).absolutePath();
                const QString vertDefault = dir + QStringLiteral("/zone.vert");
                const QString vertLegacy = dir + QStringLiteral("/zone.vert.glsl");
                vertPath = QFile::exists(vertDefault) ? vertDefault : vertLegacy;
            }

            // Set include paths for PlasmaZones shader system
            node->setShaderIncludePaths(shaderIncludePaths());

            // Clear old shader sources before loading new ones
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
            // Source empty — clear node
            node->setVertexShaderSource(QString());
            node->setFragmentShaderSource(QString());
            node->invalidateShader();
            setStatus(Status::Null);
        }
    }

    // ── Sync zone data to the node AFTER shader is ready ─────────────
    if (m_zoneDataDirty.load()) {
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

    // ── Update status based on shader node state ─────────────────────
    if (node->isShaderReady() && status() != Status::Ready) {
        setStatus(Status::Ready);
    } else if (!node->shaderError().isEmpty() && status() != Status::Error) {
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
    // Let parent handle iResolution update
    PhosphorRendering::ShaderEffect::geometryChange(newGeometry, oldGeometry);

    // Re-parse zones with new resolution for normalization
    if (newGeometry.size() != oldGeometry.size() && !m_zones.isEmpty()) {
        parseZoneData();
    }
}

void ZoneShaderItem::componentComplete()
{
    PhosphorRendering::ShaderEffect::componentComplete();

    // Parse initial zone data if any
    if (!m_zones.isEmpty()) {
        parseZoneData();
    }
}

} // namespace PlasmaZones
