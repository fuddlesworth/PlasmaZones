// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// ILayoutManager — umbrella interface aggregating every capability of
// the concrete LayoutManager. Exists so existing callers (daemon,
// adaptors) that genuinely need the full surface still have one type
// to depend on; new code should prefer the narrower sibling
// interfaces:
//
//   IZoneLayoutRegistry — enumeration + add/remove/duplicate/active-layout
//                         (QObject via PhosphorLayout::ILayoutSourceRegistry)
//   ILayoutAssignments  — per-context (screen/desktop/activity) routing
//   IQuickLayouts       — numbered 1..9 shortcut slots
//   IBuiltInLayouts     — bundled system templates
//   ILayoutPersistence  — disk I/O + import/export
//
// Design rationale: only IZoneLayoutRegistry is QObject-derived
// (through the unified ILayoutSourceRegistry notifier) — the other
// four sibling interfaces stay non-QObject. Multi-inheritance in
// ILayoutManager is therefore safe: every path leads to a single
// QObject subobject via IZoneLayoutRegistry. Avoid re-declaring
// signals in derived classes along that chain — signal shadowing on
// new-style Qt::connect is the specific hazard that motivated the
// earlier "keep everything non-QObject" rule.

#include <phosphorzones_export.h>

#include <PhosphorZones/IBuiltInLayouts.h>
#include <PhosphorZones/ILayoutAssignments.h>
#include <PhosphorZones/ILayoutPersistence.h>
#include <PhosphorZones/IQuickLayouts.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>

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
 * layouts should type against @c IZoneLayoutRegistry — it now covers
 * the read-only surface that used to live on the removed
 * @c ILayoutCatalog.
 *
 * @note Inheritance model: non-virtual multiple inheritance of the five
 * sibling interfaces. The siblings share no member state (none of
 * them define member variables). Only @c IZoneLayoutRegistry brings a
 * QObject base (via @c PhosphorLayout::ILayoutSourceRegistry) — the
 * other four stay non-QObject so every derived class reaches QObject
 * along exactly one path. Consequences for callers:
 *   - Upcasting `ILayoutManager*` to any one sibling is free and
 *     unambiguous.
 *   - Downcasting a sibling pointer (e.g. `IZoneLayoutRegistry*`) back
 *     to `ILayoutManager*` is **not** supported via `static_cast` —
 *     use `dynamic_cast` and be aware of cross-SO `typeinfo`
 *     availability, or restructure the call site to keep the wide
 *     pointer directly.
 *   - No sibling may grow member state without promoting to virtual
 *     inheritance; adding state silently multiplies the base subobject
 *     in the derived class and breaks the "upcast is free" invariant.
 *   - No sibling may become a second QObject base. The sole QObject
 *     chain runs through @c IZoneLayoutRegistry; giving another
 *     sibling its own QObject base would produce two QObject
 *     subobjects in @c LayoutManager and break @c moc.
 */
class PHOSPHORZONES_EXPORT ILayoutManager : public IZoneLayoutRegistry,
                                            public ILayoutAssignments,
                                            public IQuickLayouts,
                                            public IBuiltInLayouts,
                                            public ILayoutPersistence
{
    Q_OBJECT
public:
    explicit ILayoutManager(QObject* parent = nullptr);
    ~ILayoutManager() override;
};

} // namespace PhosphorZones
