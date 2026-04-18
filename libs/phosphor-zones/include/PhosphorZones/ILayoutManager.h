// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// ILayoutManager — umbrella interface aggregating every capability of
// the concrete LayoutManager. Exists so existing callers (daemon,
// adaptors) that genuinely need the full surface still have one type
// to depend on; new code should prefer the narrower sibling
// interfaces:
//
//   ILayoutCatalog      — read-only enumeration
//   ILayoutRegistry     — add/remove/duplicate/active-layout
//   ILayoutAssignments  — per-context (screen/desktop/activity) routing
//   IQuickLayouts       — numbered 1..9 shortcut slots
//   IBuiltInLayouts     — bundled system templates
//   ILayoutPersistence  — disk I/O + import/export
//
// Design rationale: Qt's signal system doesn't work well with
// abstract interfaces because signal shadowing between base and
// derived classes causes heap corruption when using new-style
// Qt::connect with function pointers. These interfaces are all
// non-QObject for that reason. Components needing signals should use
// LayoutManager* directly.

#include <phosphorzones_export.h>

#include <PhosphorZones/IBuiltInLayouts.h>
#include <PhosphorZones/ILayoutAssignments.h>
#include <PhosphorZones/ILayoutPersistence.h>
#include <PhosphorZones/ILayoutRegistry.h>
#include <PhosphorZones/IQuickLayouts.h>

#include <QUuid>
#include <QVector>

namespace PhosphorZones {

class Layout;

/**
 * @brief Read-only catalog of manual zone layouts.
 *
 * Minimal ISP-compliant subset used by consumers that only need to
 * enumerate and look up layouts — e.g. @c ZonesLayoutSource or any UI
 * that paints previews without mutating state. Fixture tests can stub
 * this two-method interface instead of carrying the full
 * @c ILayoutManager surface.
 *
 * Lives in this header rather than its own so consumers can
 * `#include <PhosphorZones/ILayoutManager.h>` to pick up every
 * interface in one shot; minimal-dependency consumers should prefer
 * the narrower sibling headers.
 */
class PHOSPHORZONES_EXPORT ILayoutCatalog
{
public:
    ILayoutCatalog() = default;
    virtual ~ILayoutCatalog();

    /// Enumerate every known layout. Borrowed pointers — owned by the
    /// concrete catalog (typically @c LayoutManager). Order is the
    /// catalog's natural iteration order.
    virtual QVector<Layout*> layouts() const = 0;

    /// Resolve a layout by its stable UUID. Returns nullptr when no
    /// layout with that id is known to the catalog.
    virtual Layout* layoutById(const QUuid& id) const = 0;

protected:
    ILayoutCatalog(const ILayoutCatalog&) = default;
    ILayoutCatalog& operator=(const ILayoutCatalog&) = default;
};

/**
 * @brief Full layout-management umbrella interface.
 *
 * Concrete @c LayoutManager implements this by providing every method
 * across the six sibling contracts. Callers that need the full
 * surface (e.g. D-Bus adaptors that route to every capability) type
 * against @c ILayoutManager*; narrower callers should type against
 * the specific interface they use so fixture tests stay small.
 */
class PHOSPHORZONES_EXPORT ILayoutManager : public ILayoutCatalog,
                                            public ILayoutRegistry,
                                            public ILayoutAssignments,
                                            public IQuickLayouts,
                                            public IBuiltInLayouts,
                                            public ILayoutPersistence
{
public:
    ILayoutManager() = default;
    ~ILayoutManager() override;
};

} // namespace PhosphorZones
