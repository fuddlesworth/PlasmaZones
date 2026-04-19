// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/ZonesLayoutSource.h>

#include <QUuid>

namespace PhosphorZones {

PhosphorLayout::LayoutPreview previewFromLayout(PhosphorZones::Layout* layout, const QSize& canvas)
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
    preview.aspectRatioClass = layout->aspectRatioClass();
    preview.isSystem = layout->isSystemLayout();
    // Manual preview — leave preview.algorithm as std::nullopt so
    // isAutotile() returns false.

    // Reference geometry for fixed-pixel zones. Prefer the caller's
    // canvas when provided so the projection is deterministic per-call;
    // fall back to Layout::lastRecalcGeometry() when the caller didn't
    // pass one (the historical single-screen path). The canvas parameter
    // lets two callers on different screens share a Layout* without one
    // poisoning the other's projection via the shared last-recalc cache.
    const QRectF refGeo = !canvas.isEmpty()
        ? QRectF(0, 0, canvas.width(), canvas.height())
        : (layout->hasFixedGeometryZones() ? layout->lastRecalcGeometry() : QRectF());

    if (layout->hasFixedGeometryZones() && refGeo.height() > 0) {
        preview.referenceAspectRatio = refGeo.width() / refGeo.height();
    }

    // Zone projection: relative geometry + 1-based zone numbers. Caller
    // (renderer) scales the relative rects into whatever canvas it has.
    const QRectF projectRef = layout->hasFixedGeometryZones() ? refGeo : QRectF();
    const auto zones = layout->zones();
    preview.zones.reserve(zones.size());
    preview.zoneNumbers.reserve(zones.size());
    for (PhosphorZones::Zone* zone : zones) {
        if (!zone) {
            continue;
        }
        preview.zones.append(zone->normalizedGeometry(projectRef));
        preview.zoneNumbers.append(zone->zoneNumber());
    }

    return preview;
}

// ─── ZonesLayoutSource ──────────────────────────────────────────────────────

ZonesLayoutSource::ZonesLayoutSource(PhosphorZones::IZoneLayoutRegistry* registry, QObject* parent)
    : PhosphorLayout::ILayoutSource(parent)
    , m_registry(registry)
{
    // In production the factory registrar returns nullptr from its
    // builder lambda when the ctx has no IZoneLayoutRegistry, so the
    // source is never constructed with null — asserting in debug catches
    // tests that accidentally pass nullptr instead of silently returning
    // an empty list. Release builds keep the null-tolerance branch below
    // for public API callers that legitimately want an empty source.
    Q_ASSERT_X(m_registry, "ZonesLayoutSource",
               "constructed with null IZoneLayoutRegistry — factory registrar should have returned nullptr instead");
    // Mirror AutotileLayoutSource's pattern: self-wire the registry's
    // unified contentsChanged signal (inherited from
    // PhosphorLayout::ILayoutSourceRegistry) into our own, so callers
    // don't have to bridge the registry's change signal manually.
    if (m_registry) {
        connect(m_registry, &PhosphorLayout::ILayoutSourceRegistry::contentsChanged, this,
                &PhosphorLayout::ILayoutSource::contentsChanged);
    }
}

ZonesLayoutSource::~ZonesLayoutSource() = default;

QVector<PhosphorLayout::LayoutPreview> ZonesLayoutSource::availableLayouts() const
{
    QVector<PhosphorLayout::LayoutPreview> result;
    // m_registry is borrowed; caller contract says it must outlive this
    // source (see header). Null here means the source was constructed
    // without a registry — treat as empty rather than crash.
    if (!m_registry) {
        return result;
    }

    const auto layouts = m_registry->layouts();
    result.reserve(layouts.size());
    for (PhosphorZones::Layout* layout : layouts) {
        if (!layout) {
            continue;
        }
        result.append(previewFromLayout(layout));
    }
    return result;
}

PhosphorLayout::LayoutPreview ZonesLayoutSource::previewAt(const QString& id, int /*windowCount*/, const QSize& canvas)
{
    if (!m_registry || id.isEmpty()) {
        return {};
    }
    // Explicit classifier — keeps parity with AutotileLayoutSource and makes
    // the "not mine" branch self-documenting rather than a silent parse fail.
    if (PhosphorLayout::LayoutId::isAutotile(id)) {
        return {};
    }
    const QUuid uuid = QUuid::fromString(id);
    if (uuid.isNull()) {
        return {};
    }
    PhosphorZones::Layout* layout = m_registry->layoutById(uuid);
    // Thread the caller-supplied canvas through so fixed-pixel zones
    // project against the caller's screen rather than whichever screen
    // last triggered a recalc on the shared Layout*.
    return layout ? previewFromLayout(layout, canvas) : PhosphorLayout::LayoutPreview{};
}

} // namespace PhosphorZones
