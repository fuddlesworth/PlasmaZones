// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>

#include <PhosphorLayoutApi/ILayoutSource.h>
#include <PhosphorLayoutApi/LayoutPreview.h>

namespace PhosphorTiles {
class AlgorithmRegistry;
class TilingAlgorithm;
} // namespace PhosphorTiles

namespace PhosphorTiles {

/// Convert a single TilingAlgorithm into a renderer-ready LayoutPreview.
///
/// Pure projection that runs the algorithm at @p windowCount windows
/// against a unit canvas, normalises the resulting zone rects to 0..1
/// space, and packages them with the algorithm's display metadata.
///
/// @p algorithmId is the registry id for @p algorithm — embedded into the
/// returned preview as `"autotile:<id>"`.
///
/// Provided as a free function so consumers that already hold a
/// TilingAlgorithm* can build a preview without going through
/// AutotileLayoutSource (mirrors PhosphorZones::previewFromLayout).
PHOSPHORTILES_EXPORT PhosphorLayout::LayoutPreview
previewFromAlgorithm(const QString& algorithmId, PhosphorTiles::TilingAlgorithm* algorithm,
                     int windowCount = PhosphorLayout::DefaultPreviewWindowCount);

/// Convenience overload that reverse-looks-up the algorithm id via the
/// default AlgorithmRegistry singleton. Prefer the id-taking form on hot
/// paths — the reverse lookup is O(N) per call.
PHOSPHORTILES_EXPORT PhosphorLayout::LayoutPreview
previewFromAlgorithm(PhosphorTiles::TilingAlgorithm* algorithm,
                     int windowCount = PhosphorLayout::DefaultPreviewWindowCount);

/// ILayoutSource adapter wrapping the AlgorithmRegistry singleton.
///
/// Implements PhosphorLayout::ILayoutSource so editor / settings / overlay
/// code can render autotile-algorithm previews uniformly with manual zone
/// previews from PhosphorZones::ZonesLayoutSource.  Both adapt to the same
/// LayoutPreview shape, so consumers branch on @c LayoutPreview::isAutotile
/// rather than on which concrete source they hold.
///
/// Borrows the registry — caller (typically the singleton itself, via
/// `AlgorithmRegistry::instance()`) owns it and outlives this source.
/// Source is non-copyable (matches ILayoutSource's contract).
class PHOSPHORTILES_EXPORT AutotileLayoutSource : public PhosphorLayout::ILayoutSource
{
public:
    /// Construct over a borrowed algorithm registry.  Caller owns @p registry
    /// and must keep it alive for the source's lifetime.  @p registry may be
    /// nullptr — the source then returns empty previews for every call.
    explicit AutotileLayoutSource(PhosphorTiles::AlgorithmRegistry* registry);
    ~AutotileLayoutSource() override;

    QVector<PhosphorLayout::LayoutPreview> availableLayouts() const override;

    /// @p id           must be in the form `"autotile:<algorithmId>"` —
    ///                 returns an empty preview otherwise.
    /// @p windowCount  number of windows to render the algorithm with.
    /// @p canvas       ignored at this stage; algorithms produce relative
    ///                 zones independent of canvas size.
    PhosphorLayout::LayoutPreview previewAt(const QString& id,
                                            int windowCount = PhosphorLayout::DefaultPreviewWindowCount,
                                            const QSize& canvas = {}) const override;

private:
    PhosphorTiles::AlgorithmRegistry* m_registry;
};

} // namespace PhosphorTiles
