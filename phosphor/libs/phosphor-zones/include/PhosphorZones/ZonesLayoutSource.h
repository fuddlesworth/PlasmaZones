// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorzones_export.h>

#include <PhosphorLayoutApi/ILayoutSource.h>
#include <PhosphorLayoutApi/LayoutPreview.h>

namespace PhosphorZones {
class IZoneLayoutRegistry;
class Layout;
}

namespace PhosphorZones {

/// Convert a single Layout into a renderer-ready LayoutPreview.
///
/// Pure projection — no Qt object lifecycle, no signals. Manual layouts
/// have a fixed shape, so the @c windowCount param from ILayoutSource is
/// ignored here. Provided as a free function so consumers that already
/// hold a Layout* can build a preview without going through ILayoutSource.
///
/// @p canvas (optional) — reference geometry used when projecting zones
/// that were authored in fixed-pixel mode. When empty (the default), the
/// projection falls back to @c Layout::lastRecalcGeometry(), which holds
/// whichever screen most recently triggered a recalc — fine for caller
/// topologies with a single monitor, but stale if two different screens
/// share a Layout* and query their previews in alternation. Passing the
/// caller's own canvas makes the projection deterministic per-call.
PHOSPHORZONES_EXPORT PhosphorLayout::LayoutPreview previewFromLayout(PhosphorZones::Layout* layout,
                                                                     const QSize& canvas = {});

/// ILayoutSource adapter wrapping an IZoneLayoutRegistry.
///
/// Implements PhosphorLayout::ILayoutSource so editor / settings / overlay
/// code can render manual-layout previews uniformly with autotile-algorithm
/// previews (the latter coming from PhosphorTiles::AutotileLayoutSource).
///
/// Self-wires the registry's @c contentsChanged signal (inherited from
/// @c PhosphorLayout::ILayoutSourceRegistry) to its own
/// @c contentsChanged at construction — no caller-side @c connect is
/// required. Mirrors the pattern used by @c AutotileLayoutSource.
///
/// Borrows the registry — caller owns it and must keep it alive for
/// this source's lifetime. Taking @c IZoneLayoutRegistry* rather than
/// @c ILayoutManager* means fixture tests can stub just the
/// enumeration surface (layouts() + layoutById()) instead of the full
/// manager contract.
class PHOSPHORZONES_EXPORT ZonesLayoutSource : public PhosphorLayout::ILayoutSource
{
    Q_OBJECT
public:
    /// Construct over a borrowed layout registry. Caller owns @p registry
    /// and must keep it alive for the source's lifetime.
    explicit ZonesLayoutSource(PhosphorZones::IZoneLayoutRegistry* registry, QObject* parent = nullptr);
    ~ZonesLayoutSource() override;

    QVector<PhosphorLayout::LayoutPreview> availableLayouts() const override;

    /// @p windowCount is ignored — manual layouts have authored zones.
    /// @p canvas is also ignored at this stage; future work may use it
    /// to set @c LayoutPreview::recommended for aspect-ratio filtering.
    PhosphorLayout::LayoutPreview previewAt(const QString& id,
                                            int windowCount = PhosphorLayout::DefaultPreviewWindowCount,
                                            const QSize& canvas = {}) override;

private:
    PhosphorZones::IZoneLayoutRegistry* m_registry;
};

} // namespace PhosphorZones
