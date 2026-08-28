// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/rendering/zoneshaderitem.h"

#include <QJSValue>
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
    // Re-derive the effective hover index from the REQUESTED one before the
    // parse. The two QML bindings (zones, hoveredZoneIndex) evaluate in an
    // unspecified order, so both orderings have to work: on a shrink the stale
    // effective index has to drop to -1, and on a grow an index that arrived
    // while the list was still short has to come back. Keeping the request
    // separately is what makes the second case possible — clamping destroyed it,
    // and the binding will not re-push a value that has not itself changed.
    const int oldHover = m_hoveredZoneIndex;
    m_hoveredZoneIndex = (m_requestedHoveredZoneIndex >= 0 && m_requestedHoveredZoneIndex < m_zones.size())
        ? m_requestedHoveredZoneIndex
        : -1;
    parseZoneData();

    Q_EMIT zonesChanged();
    if (m_hoveredZoneIndex != oldHover) {
        Q_EMIT hoveredZoneIndexChanged();
    }

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
    // Remember the request even when it is currently out of range, so a later
    // setZones that grows the list can honour it. See setZones for why.
    m_requestedHoveredZoneIndex = index < 0 ? -1 : index;
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

QVariant ZoneShaderItem::labelsTextureVariant() const
{
    return QVariant::fromValue(labelsTexture());
}

void ZoneShaderItem::setLabelsTextureVariant(const QVariant& labels)
{
    // Unwrapped before converting, for the reason
    // ShaderEffect::setWallpaperTextureVariant documents: QML delivers this in
    // several shapes, and a QJSValue or a QVariant nested one level answers
    // null to value<T>() rather than the payload. A host passing nothing at
    // all (null, undefined, an invalid QVariant) means "no labels", which is
    // an ordinary state and yields an empty payload.
    QVariant unwrapped = labels;
    if (unwrapped.metaType().id() == qMetaTypeId<QJSValue>()) {
        unwrapped = unwrapped.value<QJSValue>().toVariant();
    }
    for (int depth = 0; depth < 4 && unwrapped.metaType().id() == QMetaType::QVariant; ++depth) {
        unwrapped = unwrapped.value<QVariant>();
    }

    // The payload as-is, or a QImage through the converter registered in the
    // constructor (the settings and editor previews still produce one).
    if (unwrapped.canConvert<PhosphorRendering::ZoneLabelTexture>()) {
        setLabelsTexture(unwrapped.value<PhosphorRendering::ZoneLabelTexture>());
        return;
    }
    setLabelsTexture({});
}

} // namespace PlasmaZones
