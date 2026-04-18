// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorlayoutapi_export.h>

#include <PhosphorLayoutApi/ILayoutSource.h>

#include <QVector>

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
    /// source already added, is a harmless no-op.
    void addSource(ILayoutSource* source);

    /// Remove @p source from the aggregation.  No-op if @p source was never
    /// added. Does not take ownership (the composite only borrows), so the
    /// caller is responsible for deleting the source as usual.
    void removeSource(ILayoutSource* source);

    /// Drop every child source (without deleting — sources are borrowed).
    /// Handy for teardown paths that want to invalidate the composite
    /// before its children go out of scope.
    void clearSources();

    QVector<LayoutPreview> availableLayouts() const override;

    LayoutPreview previewAt(const QString& id, int windowCount = DefaultPreviewWindowCount,
                            const QSize& canvas = {}) const override;

private:
    QVector<ILayoutSource*> m_sources;
};

} // namespace PhosphorLayout
