// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

namespace PhosphorLayout {
class LayoutSourceBundle;
}

namespace PhosphorZones {
class IZoneLayoutRegistry;
}

namespace PhosphorTiles {
class ITileAlgorithmRegistry;
}

namespace PlasmaZones {

/// Populate a @c FactoryContext with the two registries every
/// PlasmaZones composition root (daemon, editor, settings) owns and
/// drive @c buildFromRegistered over it.
///
/// Centralises the "standard" context wiring so a future service
/// addition touches one helper instead of three near-identical call
/// sites in daemon.cpp / EditorController.cpp / settingscontroller.cpp.
/// Provider-side extensibility is unaffected — this helper only names
/// the services the composition roots already publish.
///
/// A composition root that needs to publish ADDITIONAL services (a
/// future @c IScrollingRegistry the root owns, say) can still call
/// @c LayoutSourceBundle::buildFromRegistered directly with a
/// hand-populated context. This helper is the default path for the
/// common case.
///
/// @pre @c bundle has not been built yet (enforced by the bundle's
///      own single-shot assertion).
/// @param zoneLayouts  Borrowed — caller owns. Required: every in-tree
///                     composition root (daemon, editor, settings)
///                     hosts a manual-layout registry; passing nullptr
///                     would silently skip the zones provider, leaving
///                     the bundle with only autotile entries (no
///                     in-tree caller wants this). If a future caller
///                     legitimately needs the no-zones case, drop the
///                     assert and add a test that exercises the path.
/// @param tileAlgorithms Borrowed — caller owns. Required: see note
///                     above; same reasoning applies symmetrically.
PLASMAZONES_EXPORT void buildStandardLayoutSourceBundle(PhosphorLayout::LayoutSourceBundle& bundle,
                                                        PhosphorZones::IZoneLayoutRegistry* zoneLayouts,
                                                        PhosphorTiles::ITileAlgorithmRegistry* tileAlgorithms);

} // namespace PlasmaZones
