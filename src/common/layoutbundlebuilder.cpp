// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutbundlebuilder.h"

#include <PhosphorLayoutApi/LayoutSourceBundle.h>
#include <PhosphorLayoutApi/LayoutSourceProviderRegistry.h>
#include <PhosphorTiles/AutotileLayoutSourceFactory.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/ZonesLayoutSourceFactory.h>

namespace PlasmaZones {

void buildStandardLayoutSourceBundle(PhosphorLayout::LayoutSourceBundle& bundle,
                                     PhosphorZones::IZoneLayoutRegistry* zoneLayouts,
                                     PhosphorTiles::ITileAlgorithmRegistry* tileAlgorithms)
{
    // Force the provider libraries' registrar TUs to link in. Under the
    // current SHARED-library build these calls are no-ops (the libraries
    // are already loaded and every TU's static init runs). Under a future
    // STATIC build + linker GC (--gc-sections, --as-needed), the linker
    // can drop TUs whose symbols are never directly referenced — silently
    // dropping the static self-registrars and shipping a bundle with
    // missing providers. Referencing these anchor symbols here keeps the
    // TUs alive regardless of link mode.
    PhosphorZones::ensureZonesLayoutSourceProviderLinked();
    PhosphorTiles::ensureAutotileLayoutSourceProviderLinked();

    PhosphorLayout::FactoryContext ctx;
    // Null services are skipped — the matching provider's builder lambda
    // returns nullptr on absent ctx entries, which the bundle treats as
    // "this composition root doesn't host that engine". Composition roots
    // that intentionally omit one registry just pass nullptr here.
    if (zoneLayouts) {
        ctx.set<PhosphorZones::IZoneLayoutRegistry>(zoneLayouts);
    }
    if (tileAlgorithms) {
        ctx.set<PhosphorTiles::ITileAlgorithmRegistry>(tileAlgorithms);
    }
    bundle.buildFromRegistered(ctx);
}

} // namespace PlasmaZones
