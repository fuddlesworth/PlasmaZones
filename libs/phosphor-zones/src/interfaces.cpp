// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/IBuiltInLayouts.h>
#include <PhosphorZones/ILayoutAssignments.h>
#include <PhosphorZones/ILayoutManager.h>
#include <PhosphorZones/ILayoutPersistence.h>
#include <PhosphorZones/IQuickLayouts.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/IZoneDetector.h>

#include <QObject>
#include <type_traits>

namespace PhosphorZones {

// ─── Inheritance-shape invariants ──────────────────────────────────────
//
// ILayoutManager non-virtually multi-inherits five sibling interfaces.
// PR #343's design pins down a single QObject base along every chain by
// (1) routing IZoneLayoutRegistry's QObject inheritance through
// PhosphorLayout::ILayoutSourceRegistry (the unified notifier base) and
// (2) keeping the four other siblings non-QObject. Adding a QObject base
// to any of those four siblings would yield two QObject subobjects in
// LayoutManager and break moc.
//
// These static_asserts make any future drift (someone re-deriving one of
// the siblings from QObject for a "small" reason) a build error rather
// than a runtime moc failure or, worse, a silent void*-round-trip
// misadventure inside FactoryContext::set/get.
static_assert(std::is_base_of_v<QObject, IZoneLayoutRegistry>,
              "IZoneLayoutRegistry must inherit QObject (via ILayoutSourceRegistry) — the unified contentsChanged "
              "signal that ZonesLayoutSource self-wires depends on it");
static_assert(std::is_base_of_v<PhosphorLayout::ILayoutSourceRegistry, IZoneLayoutRegistry>,
              "IZoneLayoutRegistry must inherit through ILayoutSourceRegistry, not directly from QObject — preserves "
              "the single-QObject-base shape across ILayoutManager's multi-inheritance chain");
static_assert(!std::is_base_of_v<QObject, ILayoutAssignments>,
              "ILayoutAssignments must stay non-QObject — adding a QObject base produces two QObject subobjects in "
              "LayoutManager and breaks moc");
static_assert(!std::is_base_of_v<QObject, IQuickLayouts>,
              "IQuickLayouts must stay non-QObject — see ILayoutAssignments note above");
static_assert(!std::is_base_of_v<QObject, IBuiltInLayouts>,
              "IBuiltInLayouts must stay non-QObject — see ILayoutAssignments note above");
static_assert(!std::is_base_of_v<QObject, ILayoutPersistence>,
              "ILayoutPersistence must stay non-QObject — see ILayoutAssignments note above");

// Out-of-line virtual destructors anchor each interface's vtable in this
// translation unit. Without it every consumer .cpp that includes one of
// these headers would emit its own weak-symbol vtable copy, bloating
// debug info and risking ODR violations across shared-library
// boundaries.
//
// ISettings + IOverlayService destructors live in src/core/interfaces.cpp
// — those interfaces stay in PZ.

IZoneLayoutRegistry::IZoneLayoutRegistry(QObject* parent)
    : PhosphorLayout::ILayoutSourceRegistry(parent)
{
}
IZoneLayoutRegistry::~IZoneLayoutRegistry() = default;
ILayoutAssignments::~ILayoutAssignments() = default;
IQuickLayouts::~IQuickLayouts() = default;
IBuiltInLayouts::~IBuiltInLayouts() = default;
ILayoutPersistence::~ILayoutPersistence() = default;
ILayoutManager::ILayoutManager(QObject* parent)
    : IZoneLayoutRegistry(parent)
{
}
ILayoutManager::~ILayoutManager() = default;

IZoneDetection::~IZoneDetection() = default;
IZoneDetector::~IZoneDetector() = default;

} // namespace PhosphorZones
