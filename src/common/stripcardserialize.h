// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Canonical serialisation of a ScrollStripSnapshot to the QML wire shape
// consumed by ZoneSelectorContent.qml's strip layer (the `stripColumns`
// property). One shape, one producer (OverlayService::buildStripList) —
// mirrors layoutpreviewserialize's role for the layout-mode popup.
//
// Column map: { tabbed: bool, active: bool, relWidth: real, relHeight: real,
//               tiles: [ { windowId, x, y, width, height, minimized,
//                          hidden, activeTab } ] }
// Tile x/y/width/height are the snapshot's column-relative fractions
// (0 for minimized / hidden tiles, which resolve no rect).
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

} // namespace PlasmaZones
