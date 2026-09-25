// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Canonical serialisation of a ScrollStripSnapshot to the QML wire shape
// consumed by ZoneSelectorContent.qml's strip layer (the `stripColumns`
// property). One shape, one producer — the strip-cards provider Daemon's
// initEngines installs, consumed through OverlayService::buildStripList —
// mirroring layoutpreviewserialize's role for the layout-mode popup.
//
// Column map: { tabbed: bool, active: bool, widthFraction: real,
//               tiles: [ { x, y, width, height, activeTab } ] }
// Tile x/y/width/height are the snapshot's column-relative fractions
// (0 for tiles that resolve no rect, e.g. the hidden tabs of a tabbed
// column). Every key here has a reader in ZoneSelectorStripCard.qml; the
// snapshot's windowId, minimized and hidden flags and the valid flag
// deliberately have no wire representation (identity is unused by the card,
// minimize is a float in this daemon so the flag can never be true here,
// hidden is derivable from tabbed + activeTab, and the popup renders "no
// strip data" and "empty strip" the same way on purpose).
//
// POSITION CONTRACT: the list index of each column, and of each tile inside
// its column, IS the DragInsertTarget index the hit-test will emit — see
// ScrollStripSnapshot's index contract in ScrollEngineTypes.h. Nothing here
// may filter or reorder.

#include "plasmazones_export.h"

#include <PhosphorScrollEngine/ScrollEngineTypes.h>

#include <QVariantList>

namespace PlasmaZones {

PLASMAZONES_EXPORT QVariantList stripColumnsToVariantList(const PhosphorScrollEngine::ScrollStripSnapshot& snapshot);

/// The widthFraction column of a serialized card list, in card order —
/// the input computeZoneSelectorLayout's variable-width bar math and the
/// trigger-edge parity path both consume. Kept beside the serializer so the
/// key spelling lives in exactly one file. Values pass through raw
/// (including a 0 for a rect-less column); stripCardPreviewWidth owns the
/// full-width fallback for out-of-range values.
PLASMAZONES_EXPORT QList<qreal> stripFractionsFromColumns(const QVariantList& columns);

} // namespace PlasmaZones
