// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// ILayoutRegistry — mutations on the catalog of manual layouts.
//
// Split out of ILayoutManager so callers that only add/remove/duplicate
// layouts (editor save path, layout-import flow, settings create-layout
// button) can depend on a 10-method contract instead of the full
// manager. Read-only enumeration lives on ILayoutCatalog; "active
// layout" selection lives here because it mutates the manager's
// active-layout slot.

#include <phosphorzones_export.h>

#include <QString>
#include <QUuid>

namespace PhosphorZones {

class Layout;

/**
 * @brief Mutation + query surface for the in-memory layout set.
 *
 * Pairs with @c ILayoutCatalog (read-only enumeration). The split
 * lets fixture tests stub just the mutation surface without
 * implementing persistence / assignments / quick-slots.
 */
class PHOSPHORZONES_EXPORT ILayoutRegistry
{
public:
    ILayoutRegistry() = default;
    virtual ~ILayoutRegistry();

    virtual int layoutCount() const = 0;
    virtual Layout* layout(int index) const = 0;
    virtual Layout* layoutByName(const QString& name) const = 0;

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

protected:
    ILayoutRegistry(const ILayoutRegistry&) = default;
    ILayoutRegistry& operator=(const ILayoutRegistry&) = default;
};

} // namespace PhosphorZones
