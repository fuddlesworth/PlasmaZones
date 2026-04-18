// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorlayoutapi_export.h>

#include <PhosphorLayoutApi/ILayoutSource.h>

#include <QHash>
#include <QVector>

#include <utility>

namespace PhosphorLayout {

/// Aggregates multiple ILayoutSource implementations behind one
/// `ILayoutSource` interface.
///
/// Use case: a consumer wants a unified view across providers (manual zone
/// layouts via `PhosphorZones::ZonesLayoutSource` + autotile previews via
/// `PhosphorTiles::AutotileLayoutSource`) but only wants to hold one
/// pointer.  Aggregation order is preserved — `availableLayouts()` returns
/// the concatenation of each child's list, in the order they were added.
///
/// `previewAt(id, ...)` walks the children in order and returns the first
/// non-empty result (children return an empty preview when they don't
/// know the id, per the `ILayoutSource` contract).  Two sources reporting
/// the same id is a configuration bug — in practice the namespaces are
/// disjoint (UUID vs `autotile:...`) so this doesn't occur in-tree.
///
/// The composite forwards each child's `contentsChanged` signal, so
/// callers can listen at the composite level only.
///
/// Borrows the child sources — caller owns each one and must keep them
/// alive for the composite's lifetime. @c addSource is idempotent (adding
/// the same pointer twice is a no-op).
class PHOSPHORLAYOUTAPI_EXPORT CompositeLayoutSource : public ILayoutSource
{
    Q_OBJECT
public:
    explicit CompositeLayoutSource(QObject* parent = nullptr);
    ~CompositeLayoutSource() override;

    /// Append a child source to the aggregation.  Passing nullptr, or a
    /// source already added, is a harmless no-op. Emits @c contentsChanged
    /// once on successful insertion — callers performing bulk wiring should
    /// use @c setSources instead to avoid an N-signal storm.
    void addSource(ILayoutSource* source);

    /// Remove @p source from the aggregation.  No-op if @p source was never
    /// added. Does not take ownership (the composite only borrows), so the
    /// caller is responsible for deleting the source as usual. Only the
    /// two connections this composite made (forwarded @c contentsChanged
    /// and @c destroyed auto-drop) are torn down — unrelated connections
    /// the caller made on @p source are left intact.
    void removeSource(ILayoutSource* source);

    /// Replace the child-source set in one shot.  Clears the existing
    /// entries, installs every non-null source from @p sources in order,
    /// and emits @c contentsChanged exactly once at the end. Intended for
    /// bulk wiring paths where incremental @c addSource calls would produce
    /// one signal per source.
    void setSources(QVector<ILayoutSource*> sources);

    /// Drop every child source (without deleting — sources are borrowed).
    /// Handy for teardown paths that want to invalidate the composite
    /// before its children go out of scope.
    void clearSources();

    QVector<LayoutPreview> availableLayouts() const override;

    LayoutPreview previewAt(const QString& id, int windowCount = DefaultPreviewWindowCount,
                            const QSize& canvas = {}) override;

private:
    /// Install the per-source change + destroyed connections and record
    /// their handles in @c m_connections so @c removeSource can tear down
    /// exactly the pair it added. Precondition: @p source is non-null and
    /// not already present.
    void connectSource(ILayoutSource* source);

    /// Disconnect the two handles recorded for @p source (if any) and drop
    /// the map entry. Safe to call on sources that are mid-destruction —
    /// the handle-based disconnect doesn't dereference the subobject.
    void disconnectSource(ILayoutSource* source);

    QVector<ILayoutSource*> m_sources;

    /// Handles for the two connections @c addSource / @c connectSource
    /// install on each child (forwarded @c contentsChanged + @c destroyed
    /// auto-drop). Stored so @c removeSource tears down only those two
    /// connections rather than nuking every connection made on @c source.
    QHash<ILayoutSource*, std::pair<QMetaObject::Connection, QMetaObject::Connection>> m_connections;
};

} // namespace PhosphorLayout
