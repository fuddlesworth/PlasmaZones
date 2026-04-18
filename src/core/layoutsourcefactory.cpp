// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutsourcefactory.h"

#include <PhosphorTiles/AutotileLayoutSource.h>
#include <PhosphorZones/ZonesLayoutSource.h>

namespace PlasmaZones {

LayoutSourceBundle::LayoutSourceBundle() = default;
LayoutSourceBundle::~LayoutSourceBundle() = default;
LayoutSourceBundle::LayoutSourceBundle(LayoutSourceBundle&&) noexcept = default;
LayoutSourceBundle& LayoutSourceBundle::operator=(LayoutSourceBundle&&) noexcept = default;

LayoutSourceBundle makeLayoutSourceBundle(PhosphorZones::ILayoutCatalog* catalog)
{
    LayoutSourceBundle bundle;
    bundle.zones = std::make_unique<PhosphorZones::ZonesLayoutSource>(catalog);
    // Autotile source defaults to AlgorithmRegistry::instance() — no need
    // to thread the singleton through every construction site.
    bundle.autotile = std::make_unique<PhosphorTiles::AutotileLayoutSource>();
    bundle.composite = std::make_unique<PhosphorLayout::CompositeLayoutSource>();
    // Source order is significant for ID-namespace precedence (manual
    // layouts use bare UUIDs; autotile uses the `autotile:` prefix, so in
    // practice IDs don't collide — but keep the well-known order anyway).
    bundle.composite->addSource(bundle.zones.get());
    bundle.composite->addSource(bundle.autotile.get());
    return bundle;
}

} // namespace PlasmaZones
