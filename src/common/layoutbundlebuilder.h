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
/// @param zoneLayouts  Borrowed — caller owns. May be nullptr in
///                     narrow cases where the composition root hosts
///                     no zone-layout engine; the zones provider will
///                     silently skip.
/// @param tileAlgorithms Borrowed — caller owns. May be nullptr.
PLASMAZONES_EXPORT void buildStandardLayoutSourceBundle(PhosphorLayout::LayoutSourceBundle& bundle,
                                                        PhosphorZones::IZoneLayoutRegistry* zoneLayouts,
                                                        PhosphorTiles::ITileAlgorithmRegistry* tileAlgorithms);

} // namespace PlasmaZones
