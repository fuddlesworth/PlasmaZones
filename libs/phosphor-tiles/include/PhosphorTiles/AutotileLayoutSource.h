// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>

// Back-compat include: previewFromAlgorithm(...) used to be declared here;
// it now lives in AutotilePreviewRender.h. Keep the include so existing
// consumers that only included <PhosphorTiles/AutotileLayoutSource.h>
// continue to pick up the free functions without modification.
#include <PhosphorTiles/AutotilePreviewRender.h>

#include <PhosphorLayoutApi/ILayoutSource.h>
#include <PhosphorLayoutApi/LayoutPreview.h>

#include <QHash>
#include <QStringList>

namespace PhosphorTiles {
class AlgorithmRegistry;
class TilingAlgorithm;
} // namespace PhosphorTiles

namespace PhosphorTiles {

/// ILayoutSource adapter wrapping the AlgorithmRegistry singleton.
///
/// Implements PhosphorLayout::ILayoutSource so editor / settings / overlay
/// code can render autotile-algorithm previews uniformly with manual zone
/// previews from PhosphorZones::ZonesLayoutSource.  Both adapt to the same
/// LayoutPreview shape, so consumers branch on @c LayoutPreview::isAutotile
/// rather than on which concrete source they hold.
///
/// Caches previews keyed on (algorithmId, windowCount) so repeated
/// queries (typical for the layout-picker UI) don't re-execute scripted
/// (JS) algorithms. The cache is invalidated when the registry emits
/// @c algorithmRegistered / @c algorithmUnregistered, at which point the
/// source also emits @c contentsChanged.
///
/// Borrows the registry — caller (typically the default singleton,
/// `AlgorithmRegistry::instance()`) owns it and outlives this source.
/// Source is non-copyable (inherited from ILayoutSource).
class PHOSPHORTILES_EXPORT AutotileLayoutSource : public PhosphorLayout::ILayoutSource
{
    Q_OBJECT
public:
    /// Construct over a borrowed algorithm registry.  Pass nullptr (the
    /// default) to bind to @c AlgorithmRegistry::instance() — this is the
    /// normal production path. Passing an explicit registry is reserved
    /// for tests that want an isolated instance.
    explicit AutotileLayoutSource(PhosphorTiles::AlgorithmRegistry* registry = nullptr, QObject* parent = nullptr);
    ~AutotileLayoutSource() override;

    QVector<PhosphorLayout::LayoutPreview> availableLayouts() const override;

    /// @p id           must be in the form `"autotile:<algorithmId>"` —
    ///                 returns an empty preview otherwise.
    /// @p windowCount  number of windows to render the algorithm with.
    /// @p canvas       ignored at this stage; algorithms produce relative
    ///                 zones independent of canvas size.
    PhosphorLayout::LayoutPreview previewAt(const QString& id,
                                            int windowCount = PhosphorLayout::DefaultPreviewWindowCount,
                                            const QSize& canvas = {}) override;

    /// Drop any cached previews — next query re-runs the algorithms.
    /// Called automatically on registry changes; exposed publicly so
    /// tests / advanced callers can trigger an explicit flush.
    void invalidateCache();

private:
    /// Insert a preview into the bounded cache, evicting the oldest entry
    /// (FIFO) when the cap `availableAlgorithms().size() * 10` is reached.
    void insertCacheEntry(const QString& key, const PhosphorLayout::LayoutPreview& preview) const;

    PhosphorTiles::AlgorithmRegistry* m_registry;
    /// Cache key is `"<algorithmId>|<windowCount>"`. Hash preserves
    /// insertion-order-independent lookup semantics.
    mutable QHash<QString, PhosphorLayout::LayoutPreview> m_cache;
    /// FIFO eviction order — keys appended on insert, head evicted on overflow.
    mutable QStringList m_cacheOrder;
    /// Cached `availableAlgorithms().size()` — refreshed on every
    /// registry-change signal via invalidateCache(). Without the cache,
    /// each insertCacheEntry() re-queries the registry (returning a fresh
    /// QStringList) just to compute the FIFO cap; for picker UIs that
    /// churn the cache repeatedly that adds up.
    mutable int m_algorithmCountCache = 0;
};

} // namespace PhosphorTiles
