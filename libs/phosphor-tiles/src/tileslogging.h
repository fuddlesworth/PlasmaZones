// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Tiles-local logging categories.  Distinct names from the daemon-side
// lcAutotile category so we don't collide at link time — daemon and library
// each own their own log filtering knob (org.plasmazones.autotile /
// org.phosphor.tiles respectively).

#include <QLoggingCategory>

namespace PhosphorTiles {

Q_DECLARE_LOGGING_CATEGORY(lcTilesLib)

} // namespace PhosphorTiles
