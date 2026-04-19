// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutbundlebuilder.h"

#include <PhosphorLayoutApi/LayoutSourceBundle.h>
#include <PhosphorLayoutApi/LayoutSourceProviderRegistry.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>

namespace PlasmaZones {

void buildStandardLayoutSourceBundle(PhosphorLayout::LayoutSourceBundle& bundle,
                                     PhosphorZones::IZoneLayoutRegistry* zoneLayouts,
                                     PhosphorTiles::ITileAlgorithmRegistry* tileAlgorithms)
{
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
