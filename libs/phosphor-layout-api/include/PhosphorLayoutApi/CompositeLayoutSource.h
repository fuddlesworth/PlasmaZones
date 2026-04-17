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
/// the same id is a configuration bug — the first one wins.
///
/// Borrows the child sources — caller owns each one and must keep them
/// alive for the composite's lifetime.
class PHOSPHORLAYOUTAPI_EXPORT CompositeLayoutSource : public ILayoutSource
{
public:
    CompositeLayoutSource() = default;
    ~CompositeLayoutSource() override;

    /// Append a child source to the aggregation.  Pass nullptr is harmless
    /// (the slot is skipped at query time) — keeps construction sites
    /// terse when a child source is conditionally available.
    void addSource(ILayoutSource* source);

    QVector<LayoutPreview> availableLayouts() const override;

    LayoutPreview previewAt(const QString& id, int windowCount = 4, const QSize& canvas = {}) const override;

private:
    QVector<ILayoutSource*> m_sources;
};

} // namespace PhosphorLayout
