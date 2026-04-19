// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTiles/ITileAlgorithmRegistry.h>

#include <type_traits>

namespace PhosphorTiles {

// Inheritance-shape invariants — symmetric with the IZoneLayoutRegistry
// asserts in libs/phosphor-zones/src/interfaces.cpp. The unified
// contentsChanged signal lives on ILayoutSourceRegistry; concrete
// registries must reach QObject through it (single QObject base) so
// AutotileLayoutSource's self-wired connect() targets the right
// signal table and FactoryContext::set/get round-trip stays at offset 0.
static_assert(std::is_base_of_v<QObject, ITileAlgorithmRegistry>,
              "ITileAlgorithmRegistry must inherit QObject (via ILayoutSourceRegistry)");
static_assert(std::is_base_of_v<PhosphorLayout::ILayoutSourceRegistry, ITileAlgorithmRegistry>,
              "ITileAlgorithmRegistry must inherit through ILayoutSourceRegistry, not directly from QObject");

ITileAlgorithmRegistry::ITileAlgorithmRegistry(QObject* parent)
    : PhosphorLayout::ILayoutSourceRegistry(parent)
{
}

ITileAlgorithmRegistry::~ITileAlgorithmRegistry() = default;

} // namespace PhosphorTiles
