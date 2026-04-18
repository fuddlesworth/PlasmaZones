// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/ILayoutManager.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/ZonesLayoutSource.h>

#include <QUuid>

namespace PhosphorZones {

PhosphorLayout::LayoutPreview previewFromLayout(PhosphorZones::Layout* layout)
{
    PhosphorLayout::LayoutPreview preview;
    if (!layout) {
        return preview;
    }

    preview.id = layout->id().toString();
    preview.displayName = layout->name();
    preview.description = layout->description();
    preview.zoneCount = layout->zoneCount();
    preview.autoAssign = layout->autoAssign();
    preview.aspectRatioClass = static_cast<int>(layout->aspectRatioClass());
    preview.isSystem = layout->isSystemLayout();
    // Manual preview — leave preview.algorithm as std::nullopt so
    // isAutotile() returns false.

    if (layout->hasFixedGeometryZones()) {
        const QRectF refGeo = layout->lastRecalcGeometry();
        if (refGeo.height() > 0) {
            preview.referenceAspectRatio = refGeo.width() / refGeo.height();
        }
    }

    // Zone projection: relative geometry + 1-based zone numbers. Caller
    // (renderer) scales the relative rects into whatever canvas it has.
    const QRectF refGeo = layout->hasFixedGeometryZones() ? layout->lastRecalcGeometry() : QRectF();
    const auto zones = layout->zones();
    preview.zones.reserve(zones.size());
    preview.zoneNumbers.reserve(zones.size());
    for (PhosphorZones::Zone* zone : zones) {
        if (!zone) {
            continue;
        }
        preview.zones.append(zone->normalizedGeometry(refGeo));
        preview.zoneNumbers.append(zone->zoneNumber());
    }

    return preview;
}

// ─── ZonesLayoutSource ──────────────────────────────────────────────────────

ZonesLayoutSource::ZonesLayoutSource(PhosphorZones::ILayoutCatalog* catalog, QObject* parent)
    : PhosphorLayout::ILayoutSource(parent)
    , m_catalog(catalog)
{
}

ZonesLayoutSource::~ZonesLayoutSource() = default;

QVector<PhosphorLayout::LayoutPreview> ZonesLayoutSource::availableLayouts() const
{
    QVector<PhosphorLayout::LayoutPreview> result;
    // m_catalog is borrowed; caller contract says it must outlive this
    // source (see header). Null here means the source was constructed
    // without a catalog — treat as empty rather than crash.
    if (!m_catalog) {
        return result;
    }

    const auto layouts = m_catalog->layouts();
    result.reserve(layouts.size());
    for (PhosphorZones::Layout* layout : layouts) {
        if (!layout) {
            continue;
        }
        result.append(previewFromLayout(layout));
    }
    return result;
}

PhosphorLayout::LayoutPreview ZonesLayoutSource::previewAt(const QString& id, int /*windowCount*/,
                                                           const QSize& /*canvas*/) const
{
    if (!m_catalog || id.isEmpty()) {
        return {};
    }
    const QUuid uuid = QUuid::fromString(id);
    if (uuid.isNull()) {
        return {};
    }
    PhosphorZones::Layout* layout = m_catalog->layoutById(uuid);
    return layout ? previewFromLayout(layout) : PhosphorLayout::LayoutPreview{};
}

void ZonesLayoutSource::notifyContentsChanged()
{
    Q_EMIT contentsChanged();
}

} // namespace PhosphorZones
