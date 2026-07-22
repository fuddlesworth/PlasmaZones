// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/rendering/zoneshaderitem.h"

#include "core/types/constants.h"
#include "core/platform/logging.h"

#include <QMutexLocker>

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

PhosphorRendering::ZoneLabelTexture ZoneShaderItem::labelsTexture() const
{
    QMutexLocker lock(&m_labelsTextureMutex);
    return m_labelsTexture;
}

void ZoneShaderItem::setLabelsTexture(const PhosphorRendering::ZoneLabelTexture& labels)
{
    // Emit only on a genuine change (project rule). The daemon overlay path
    // already dedupes upstream via labelsTextureHash, but the editor/settings
    // preview + placeholder paths don't, so guard here. The compare short-
    // circuits on size before any per-tile pixel compare.
    {
        QMutexLocker lock(&m_labelsTextureMutex);
        if (m_labelsTexture == labels) {
            return;
        }
        m_labelsTexture = labels;
    }
    Q_EMIT labelsTextureChanged();
    update();
}

} // namespace PlasmaZones
