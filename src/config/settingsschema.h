// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// PlasmaZones settings schema: declarative description of every key that
// Settings persists, for use with PhosphorConfig::Store.
//
// Built incrementally — each call to appendXxx() adds one group's worth of
// keys to the schema. Migration from the hand-written load*/save* functions
// in settings.cpp proceeds one group at a time; an unmigrated group simply
// isn't in the schema yet, and Settings continues to reach the backend
// directly for those groups.

#include "plasmazones_export.h"

#include <PhosphorConfig/Schema.h>

namespace PlasmaZones {

/// Returns the full PZ settings schema, populated with every group migrated
/// to PhosphorConfig::Store so far. Called once during Settings construction
/// and handed to Store; add new groups here as they're migrated.
PLASMAZONES_EXPORT PhosphorConfig::Schema buildSettingsSchema();

// ─── Group helpers ──────────────────────────────────────────────────────────
// Each helper appends one group's KeyDefs to the schema. Kept as free
// functions so the migration can add them one at a time without touching
// a monolithic switch statement.

void appendShadersSchema(PhosphorConfig::Schema& schema);
void appendAppearanceSchema(PhosphorConfig::Schema& schema);

} // namespace PlasmaZones
