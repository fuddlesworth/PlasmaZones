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
// PhosphorZones::Zone Data Setters
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
    // No cacheKey() short-circuit: every detached copy of an identical QImage
    // gets a new cacheKey, so the guard would almost never hit and would hide
    // the ordinary case where rebuilds always flow through. Size/format/hash
    // dedupe is already handled upstream in OverlayService::updateLabelsTextureForWindow
    // via labelsTextureHash; here we just accept the incoming image.
    {
        QMutexLocker lock(&m_labelsTextureMutex);
        m_labelsTexture = image;
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

    // Base class stores the map and parses the canonical slot-keyed
    // entries (customParams1_x..customParams8_w into customParams[8],
    // customColor1..customColor16 into customColors[16]). Anything that
    // overflows that contract — uTexture<N> path / wrap / svgSize, the
    // ZoneShaderItem-only image-loading fields below — we still own.
    PhosphorRendering::ShaderEffect::setShaderParams(params);

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
