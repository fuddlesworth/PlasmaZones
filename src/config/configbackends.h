// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// PlasmaZones-side configuration backend factories.
//
// Each factory materializes a PhosphorConfig::JsonBackend pointed at the
// relevant PZ config file, with PZ's per-screen path resolver attached and
// the current schema version wired for fresh-install stamping.

#include "plasmazones_export.h"

#include <PhosphorConfig/IBackend.h>
#include <PhosphorConfig/JsonBackend.h>
#include <PhosphorConfig/QSettingsBackend.h>

#include <memory>

namespace PlasmaZones {

/// Primary user config (~/.config/plasmazones/config.json).
PLASMAZONES_EXPORT std::unique_ptr<PhosphorConfig::IBackend> createDefaultConfigBackend();

/// Ephemeral window-tracking session state (~/.config/plasmazones/session.json).
/// Separate file from config.json to avoid write contention.
PLASMAZONES_EXPORT std::unique_ptr<PhosphorConfig::IBackend> createSessionBackend();

/// PhosphorZones::Layout assignments and quick-layout shortcuts
/// (~/.config/plasmazones/assignments.json). LayoutManager owns its own file
/// so its persistence is independent from user preferences.
PLASMAZONES_EXPORT std::unique_ptr<PhosphorConfig::IBackend> createAssignmentsBackend();

/// Legacy plasmazonesrc INI backend. Used only by migration and tests —
/// runtime code uses the JSON factories above.
PLASMAZONES_EXPORT std::unique_ptr<PhosphorConfig::QSettingsBackend> createLegacyQSettingsBackend();

} // namespace PlasmaZones
