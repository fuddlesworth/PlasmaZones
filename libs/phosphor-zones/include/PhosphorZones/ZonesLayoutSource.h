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
/// @note ILayoutCatalog is not a QObject (see PhosphorZones::ILayoutManager
/// for the rationale — signal shadowing in abstract-interface hierarchies).
/// Callers that want this source to emit @c contentsChanged when their
/// underlying catalog changes must wire the catalog's change signal
/// explicitly to @c notifyContentsChanged:
///
/// @code
///   connect(layoutManager, &LayoutManager::layoutsChanged,
///           zonesSource,   &ZonesLayoutSource::notifyContentsChanged);
/// @endcode
///
/// Borrows the catalog — caller owns it and must keep it alive for this
/// source's lifetime.  Taking ILayoutCatalog* rather than
/// ILayoutManager* means fixture tests can stub just two methods
/// (layouts() + layoutById()) instead of the full manager contract.
class PHOSPHORZONES_EXPORT ZonesLayoutSource : public PhosphorLayout::ILayoutSource
{
    Q_OBJECT
public:
    /// Construct over a borrowed layout catalog. Caller owns @p catalog
    /// and must keep it alive for the source's lifetime.
    explicit ZonesLayoutSource(PhosphorZones::ILayoutCatalog* catalog, QObject* parent = nullptr);
    ~ZonesLayoutSource() override;

    QVector<PhosphorLayout::LayoutPreview> availableLayouts() const override;

    /// @p windowCount is ignored — manual layouts have authored zones.
    /// @p canvas is also ignored at this stage; future work may use it
    /// to set @c LayoutPreview::recommended for aspect-ratio filtering.
    PhosphorLayout::LayoutPreview previewAt(const QString& id,
                                            int windowCount = PhosphorLayout::DefaultPreviewWindowCount,
                                            const QSize& canvas = {}) const override;

public Q_SLOTS:
    /// Caller-driven re-emit of @c contentsChanged. Hook this into the
    /// owning LayoutManager's layout-set-changed signal (see class doc).
    void notifyContentsChanged();

private:
    PhosphorZones::ILayoutCatalog* m_catalog;
};

} // namespace PhosphorZones
