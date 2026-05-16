// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

namespace PlasmaZones {

/// Install the PhosphorZones::Layout::setScreenIdResolver translator for this
/// process exactly once. Normalises legacy connector names ("DP-2") to
/// EDID-based IDs ("LG:Model:Serial") via
/// Phosphor::Screens::ScreenIdentity::idForName so layouts loaded from disk
/// (including per-screen allowedScreens entries) resolve consistently
/// regardless of whether the config was authored on the current hardware.
///
/// Safe to call repeatedly and from any composition root (daemon, editor,
/// settings). The underlying install happens exactly once per process via
/// a Meyer's singleton guard — subsequent calls are no-ops.
///
/// Call before any layout load runs. Each composition root calls this from
/// its ctor body (moved out of the `QObject((ensureScreenIdResolver(),
/// parent))` comma-operator trick so the intent is obvious at a glance).
PLASMAZONES_EXPORT void ensureScreenIdResolver();

} // namespace PlasmaZones
