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
class ITileAlgorithmRegistry;
class TilingAlgorithm;
} // namespace PhosphorTiles

namespace PhosphorTiles {

/// ILayoutSource adapter wrapping an ITileAlgorithmRegistry.
///
/// Implements PhosphorLayout::ILayoutSource so editor / settings / overlay
/// code can render autotile-algorithm previews uniformly with manual zone
/// previews from PhosphorZones::ZonesLayoutSource.  Both adapt to the same
/// LayoutPreview shape, so consumers branch on @c LayoutPreview::isAutotile
/// rather than on which concrete source they hold.
///
/// Caches previews keyed on (algorithmId, windowCount) so repeated
/// queries (typical for the layout-picker UI) don't re-execute scripted
/// (JS) algorithms. The cache is invalidated on the registry's unified
/// @c ILayoutSourceRegistry::contentsChanged signal (covering algorithm
/// register/unregister and preview-params changes), at which point the
/// source also emits @c contentsChanged.
///
/// Borrows the registry — caller owns it and must keep it alive for the
/// source's lifetime. Source is non-copyable (inherited from
/// ILayoutSource).
class PHOSPHORTILES_EXPORT AutotileLayoutSource : public PhosphorLayout::ILayoutSource
{
    Q_OBJECT
public:
    /// Construct over a borrowed algorithm registry. Composition roots
    /// (daemon, editor, settings, tests) create their own
    /// @c AlgorithmRegistry instance and thread it here. Null is tolerated
    /// (source returns empty) — matches @c ZonesLayoutSource's discipline.
    explicit AutotileLayoutSource(PhosphorTiles::ITileAlgorithmRegistry* registry, QObject* parent = nullptr);
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

    PhosphorTiles::ITileAlgorithmRegistry* m_registry;
    /// Cache key is `"<algorithmId>|<windowCount>"`. Hash preserves
    /// insertion-order-independent lookup semantics.
    mutable QHash<QString, PhosphorLayout::LayoutPreview> m_cache;
    /// FIFO eviction order — keys appended on insert, head evicted on overflow.
    mutable QStringList m_cacheOrder;
};

} // namespace PhosphorTiles
