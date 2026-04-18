// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorzones_export.h>

#include <PhosphorLayoutApi/ILayoutSource.h>
#include <PhosphorLayoutApi/LayoutPreview.h>

namespace PhosphorZones {
class ILayoutCatalog;
class Layout;
}

namespace PhosphorZones {

/// Convert a single Layout into a renderer-ready LayoutPreview.
///
/// Pure projection — no Qt object lifecycle, no signals. Manual layouts
/// have a fixed shape, so the @c windowCount param from ILayoutSource is
/// ignored here. Provided as a free function so consumers that already
/// hold a Layout* can build a preview without going through ILayoutSource.
PHOSPHORZONES_EXPORT PhosphorLayout::LayoutPreview previewFromLayout(PhosphorZones::Layout* layout);

/// ILayoutSource adapter wrapping an ILayoutCatalog.
///
/// Implements PhosphorLayout::ILayoutSource so editor / settings / overlay
/// code can render manual-layout previews uniformly with autotile-algorithm
/// previews (the latter coming from PhosphorTiles::AutotileLayoutSource).
///
/// Borrows the catalog — caller owns it. Source is non-copyable (matches
/// ILayoutSource's contract). Taking ILayoutCatalog* rather than
/// ILayoutManager* means fixture tests can stub just two methods
/// (layouts() + layoutById()) instead of the full manager contract.
class PHOSPHORZONES_EXPORT ZonesLayoutSource : public PhosphorLayout::ILayoutSource
{
public:
    /// Construct over a borrowed layout catalog. Caller owns @p catalog
    /// and must keep it alive for the source's lifetime.
    explicit ZonesLayoutSource(PhosphorZones::ILayoutCatalog* catalog);
    ~ZonesLayoutSource() override;

    QVector<PhosphorLayout::LayoutPreview> availableLayouts() const override;

    /// @p windowCount is ignored — manual layouts have authored zones.
    /// @p canvas is also ignored at this stage; future work may use it
    /// to set @c LayoutPreview::recommended for aspect-ratio filtering.
    PhosphorLayout::LayoutPreview previewAt(const QString& id,
                                            int windowCount = PhosphorLayout::DefaultPreviewWindowCount,
                                            const QSize& canvas = {}) const override;

private:
    PhosphorZones::ILayoutCatalog* m_catalog;
};

} // namespace PhosphorZones
