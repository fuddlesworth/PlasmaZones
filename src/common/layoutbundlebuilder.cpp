// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutbundlebuilder.h"

#include <PhosphorLayoutApi/LayoutSourceBundle.h>
#include <PhosphorLayoutApi/LayoutSourceProviderRegistry.h>
#include <PhosphorTiles/AutotileLayoutSourceFactory.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/ZonesLayoutSourceFactory.h>

#include <QtGlobal>

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

    // Both registries are required by every in-tree caller. The previous
    // null-tolerant branches were dead code — kept as safety guards but
    // never exercised, so any silent regression in caller wiring would
    // have shipped without a test catching it. Asserting up-front turns
    // a wiring bug into a build/dev-time failure rather than a bundle
    // that silently ships missing one provider.
    Q_ASSERT_X(zoneLayouts, "buildStandardLayoutSourceBundle",
               "zoneLayouts is required — every in-tree composition root hosts a manual-layout registry");
    Q_ASSERT_X(tileAlgorithms, "buildStandardLayoutSourceBundle",
               "tileAlgorithms is required — every in-tree composition root hosts a tile-algorithm registry");

    PhosphorLayout::FactoryContext ctx;
    // Null fallback retained for release builds: the matching provider's
    // builder lambda returns nullptr on absent ctx entries, which the
    // bundle treats as "this composition root doesn't host that engine".
    // A null caller in release degrades to a partial bundle rather than
    // a crash; debug catches it via the asserts above.
    if (zoneLayouts) {
        ctx.set<PhosphorZones::IZoneLayoutRegistry>(zoneLayouts);
    }
    if (tileAlgorithms) {
        ctx.set<PhosphorTiles::ITileAlgorithmRegistry>(tileAlgorithms);
    }
    bundle.buildFromRegistered(ctx);
}

} // namespace PlasmaZones
