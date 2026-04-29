// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// IZoneLayoutRegistry — enumeration + mutation of the catalog of
// manual zone layouts.
//
// Split out of ILayoutManager so callers that need the layout set
// (editor save path, layout-import flow, settings create-layout
// button, read-only preview renderers) can depend on one contract
// instead of the full manager. "Active layout" selection lives here
// because it mutates the manager's active-layout slot.
//
// Inherits PhosphorLayout::ILayoutSourceRegistry so concrete registries
// (LayoutManager) carry the unified `contentsChanged` signal that
// ZonesLayoutSource subscribes to — matching the pattern every other
// provider library (phosphor-tiles, future phosphor-scrolling, …)
// follows. Inheriting QObject via the unified base rather than
// directly keeps ILayoutManager's non-virtual multi-inheritance safe:
// every path through ILayoutManager reaches QObject exactly once, so
// LayoutManager has a single QObject subobject.

#include <phosphorzones_export.h>

#include <PhosphorLayoutApi/ILayoutSourceRegistry.h>

#include <QString>
#include <QUuid>
#include <QVector>

namespace PhosphorZones {

class Layout;

/**
 * @brief Enumeration + mutation surface for the in-memory zone-layout
 * catalog.
 *
 * Fixture tests can stub this contract without implementing
 * persistence / assignments / quick-slots.
 */
class PHOSPHORZONES_EXPORT IZoneLayoutRegistry : public PhosphorLayout::ILayoutSourceRegistry
{
    Q_OBJECT
public:
    explicit IZoneLayoutRegistry(QObject* parent = nullptr);
    ~IZoneLayoutRegistry() override;

    /// Enumerate every known layout. Borrowed pointers — owned by the
    /// concrete registry (typically @c LayoutManager). Order is the
    /// registry's natural iteration order.
    virtual QVector<Layout*> layouts() const = 0;

    virtual int layoutCount() const = 0;
    virtual Layout* layout(int index) const = 0;
    virtual Layout* layoutByName(const QString& name) const = 0;

    /// Resolve a layout by its stable UUID. Returns nullptr when no
    /// layout with that id is known to the registry.
    virtual Layout* layoutById(const QUuid& id) const = 0;

    /// @param layout Ownership transferred — the registry adopts @p layout
    ///               and is responsible for its lifetime from this call on.
    virtual void addLayout(Layout* layout) = 0;
    /// @param layout Borrowed — caller retains ownership. Registry
    ///               un-registers but does NOT delete.
    virtual void removeLayout(Layout* layout) = 0;
    virtual void removeLayoutById(const QUuid& id) = 0;
    /// @param source Borrowed — caller retains ownership.
    /// @return       Newly allocated copy; ownership transferred to the
    ///               registry (mirrors @c addLayout semantics). Returns
    ///               nullptr if @p source is unknown.
    virtual Layout* duplicateLayout(Layout* source) = 0;

    // Active layout (internal — used for resnap / geometry / overlay
    // machinery). Borrowed pointer owned by the registry.
    virtual Layout* activeLayout() const = 0;
    virtual void setActiveLayout(Layout* layout) = 0;
    virtual void setActiveLayoutById(const QUuid& id) = 0;
};

} // namespace PhosphorZones
