// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// ILayoutManager — umbrella interface aggregating every capability of
// the concrete LayoutManager. Exists so existing callers (daemon,
// adaptors) that genuinely need the full surface still have one type
// to depend on; new code should prefer the narrower sibling
// interfaces:
//
//   ILayoutRegistry     — enumeration + add/remove/duplicate/active-layout
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
 * @brief Full layout-management umbrella interface.
 *
 * Concrete @c LayoutManager implements this by providing every method
 * across the five sibling contracts. Callers that need the full
 * surface (e.g. D-Bus adaptors that route to every capability) type
 * against @c ILayoutManager*; narrower callers should type against
 * the specific interface they use so fixture tests stay small. In
 * particular, components that only need to enumerate / look up
 * layouts should type against @c ILayoutRegistry — it now covers the
 * read-only surface that used to live on the removed
 * @c ILayoutCatalog.
 */
class PHOSPHORZONES_EXPORT ILayoutManager : public ILayoutRegistry,
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
