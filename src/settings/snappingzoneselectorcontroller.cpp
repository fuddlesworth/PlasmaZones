// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Intentionally light: SnappingZoneSelectorController is a pure Q_PROPERTY
// facade over ConfigDefaults constants. All getters are inline in the header.
// This file exists so CMake AUTOMOC emits moc output into the settings target
// and matches the file layout of the other sub-controllers.

#include "snappingzoneselectorcontroller.h"
