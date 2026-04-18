// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutsourcefactory.h"

#include <PhosphorTiles/AutotileLayoutSource.h>
#include <PhosphorZones/ZonesLayoutSource.h>

namespace PlasmaZones {

LayoutSourceBundle::LayoutSourceBundle() = default;
LayoutSourceBundle::LayoutSourceBundle(LayoutSourceBundle&&) noexcept = default;

// Hand-written destructor — a defaulted one would rely on reverse declaration
// order (composite → autotile → zones) to drop the composite's borrowed
// pointers before their target objects destruct. That works today, but a
// future reorder of the struct members would silently reverse the order and
// introduce a UAF. Clear the composite explicitly here so destruction-order
// correctness doesn't hinge on member-declaration order staying put.
LayoutSourceBundle::~LayoutSourceBundle()
{
    if (composite) {
        composite->clearSources();
    }
}

// Hand-written move-assign — a defaulted implementation would move
// `zones`, then `autotile`, then `composite` member-wise, which means the
// *old* LHS composite keeps its raw pointers into the *old* LHS zones /
// autotile alive for one member-move step each. It's latent today because
// no signals fire in the interim on single-threaded paths, but any future
// signal emission from a child during destruction would race.
// Drop the old composite's child pointers first, then let the member
// moves proceed in declaration order.
LayoutSourceBundle& LayoutSourceBundle::operator=(LayoutSourceBundle&& other) noexcept
{
    if (this != &other) {
        if (composite) {
            composite->clearSources();
        }
        zones = std::move(other.zones);
        autotile = std::move(other.autotile);
        composite = std::move(other.composite);
    }
    return *this;
}

LayoutSourceBundle makeLayoutSourceBundle(PhosphorZones::ILayoutRegistry* registry)
{
    LayoutSourceBundle bundle;
    bundle.zones = std::make_unique<PhosphorZones::ZonesLayoutSource>(registry);
    // Autotile source defaults to AlgorithmRegistry::instance() — no need
    // to thread the singleton through every construction site.
    bundle.autotile = std::make_unique<PhosphorTiles::AutotileLayoutSource>();
    bundle.composite = std::make_unique<PhosphorLayout::CompositeLayoutSource>();
    // Source order is significant for ID-namespace precedence (manual
    // layouts use bare UUIDs; autotile uses the `autotile:` prefix, so in
    // practice IDs don't collide — but keep the well-known order anyway).
    // setSources emits contentsChanged exactly once; incremental addSource
    // calls would fire N times during bulk wiring.
    bundle.composite->setSources({bundle.zones.get(), bundle.autotile.get()});
    return bundle;
}

} // namespace PlasmaZones
