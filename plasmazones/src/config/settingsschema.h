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

/// Shared immortal schema for READ-ONLY consumers (choice lookups, value
/// resolution). Built once on first use. Settings must NOT use this — it
/// hands its Schema to a Store, which owns a copy per instance.
PLASMAZONES_EXPORT const PhosphorConfig::Schema& cachedSettingsSchema();

// ─── Group helpers ──────────────────────────────────────────────────────────
// Each helper appends one group's KeyDefs to the schema. Kept as free
// functions so the migration can add them one at a time without touching
// a monolithic switch statement.

/// Canonicalize a trigger list (cap size, coerce entries to
/// {modifier:int, mouseButton:int} maps). Defined in settingsschema.cpp;
/// shared with the per-domain schema TUs because trigger-list keys now span
/// domains (Tiling.Behavior and Scrolling.Behavior).
QVariant canonicalTriggerList(const QVariant& v);

/// canonicalTriggerList plus a drop of the AlwaysActive sentinel, for the
/// wheel chord lists that are read with exact matching. See the definition
/// for why the sentinel cannot travel that path.
QVariant canonicalWheelTriggerList(const QVariant& v);

/// Canonicalize a theme-fallback colour: keep EMPTY (the "follow the colour
/// scheme" sentinel) and any string QColor can parse, and drop anything else
/// back to empty. The D-Bus boundary already refuses an unparseable colour,
/// but the DISK path does not go through it — a hand-edited config reached
/// QML unchecked and Qt paints an invalid QColor as black. Shared by every
/// theme-fallback colour key: the zone colours, the Windows border/tint
/// colours, the tab indicator's three, and the drop indicator's two.
QVariant canonicalThemeFallbackColor(const QVariant& v);

/// Bound the animation-shader assignment tree at the persistence boundary:
/// string lengths, parameter-map size and value shapes, override count. The
/// twin of sanitizeOverlayShaderTree, sharing its bounds; see
/// settingsschema_shadertrees.cpp for why the motion keys beside it cannot get
/// the same treatment.
QVariant sanitizeShaderProfileTree(const QVariant& v);

/// The decoration-tree twin of the above. Its parameter map is nested one
/// level deeper ({packId -> {paramId -> value}}), so only the inner level is
/// bounded scalar-only.
QVariant sanitizeDecorationProfileTree(const QVariant& v);

/// The MOTION tree's read-side bound, which simply runs the same canonicaliser its
/// setter does (`canonicalMotionProfileTree`, settings/profiletrees.cpp).
///
/// Registered because the setter cannot be the only boundary: configmigration_v8 writes
/// this key directly into the JSON document, outside the store, and config.json is
/// hand-editable, so neither path ever passed through a setter. Sharing one function is
/// what keeps the "canonicalise and bound on one pass" property the setter relies on.
QVariant sanitizeMotionProfileTree(const QVariant& v);

/// The motion tree's persistence form, shared by the setter and the sanitizer above.
QVariantMap canonicalMotionProfileTree(const QVariantMap& tree);

void appendShadersSchema(PhosphorConfig::Schema& schema);
void appendOverlayShadersSchema(PhosphorConfig::Schema& schema);
void appendAppearanceSchema(PhosphorConfig::Schema& schema);
void appendOrderingSchema(PhosphorConfig::Schema& schema);
void appendAnimationsSchema(PhosphorConfig::Schema& schema);
void appendRenderingSchema(PhosphorConfig::Schema& schema);
void appendPerformanceSchema(PhosphorConfig::Schema& schema);
void appendZoneGeometrySchema(PhosphorConfig::Schema& schema);
void appendShortcutsSchema(PhosphorConfig::Schema& schema);
void appendEditorSchema(PhosphorConfig::Schema& schema);
void appendExclusionsSchema(PhosphorConfig::Schema& schema);
void appendDisplaySchema(PhosphorConfig::Schema& schema);
void appendZoneSelectorSchema(PhosphorConfig::Schema& schema);
void appendActivationSchema(PhosphorConfig::Schema& schema);
void appendBehaviorSchema(PhosphorConfig::Schema& schema);
// The tiling entry point lives in settingsschema_tiling.cpp (split out for
// file-size, the scrolling shape).
void appendAutotilingSchema(PhosphorConfig::Schema& schema);
// The three scrolling entry points live in settingsschema_scrolling.cpp (split
// out for file-size). appendScrollingShortcutsSchema is called from
// appendShortcutsSchema, not from buildSettingsSchema.
void appendScrollingSchema(PhosphorConfig::Schema& schema);
void appendScrollingZoneSelectorSchema(PhosphorConfig::Schema& schema);
void appendScrollingShortcutsSchema(PhosphorConfig::Schema& schema);
void appendWindowsSchema(PhosphorConfig::Schema& schema);
void appendGapsSchema(PhosphorConfig::Schema& schema);
void appendDecorationsSchema(PhosphorConfig::Schema& schema);

} // namespace PlasmaZones
