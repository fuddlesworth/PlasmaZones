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

#include <memory>

namespace PlasmaZones {

/// Primary user config (~/.config/plasmazones/config.json).
PLASMAZONES_EXPORT std::unique_ptr<PhosphorConfig::IBackend> createDefaultConfigBackend();

/// Ephemeral window-tracking session state (~/.config/plasmazones/session.json).
/// Separate file from config.json to avoid write contention.
PLASMAZONES_EXPORT std::unique_ptr<PhosphorConfig::IBackend> createSessionBackend();

} // namespace PlasmaZones
