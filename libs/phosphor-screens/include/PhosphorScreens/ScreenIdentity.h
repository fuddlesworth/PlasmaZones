// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "phosphorscreens_export.h"

// EDID parsing primitives + the wire-format helpers (`buildBaseId`,
// `buildScreenBaseId`, `normalizeHexSerial`, `readEdidHeaderSerial`,
// `invalidateEdidCache`) live in PhosphorIdentity::ScreenId — they're
// cross-process and the KWin effect needs them without pulling in
// Qt6::Gui. Re-include here so callers of this header don't have to
// chase the indirection.
#include <PhosphorIdentity/ScreenId.h>

#include <QString>

class QScreen;

namespace Phosphor::Screens {

/**
 * @brief Stable cross-process screen identifier helpers.
 *
 * Owns the canonical "manufacturer:model:serial" wire format that
 * PhosphorScreens uses to refer to a physical screen across processes
 * (daemon, KCM, compositor plugin). Hidden behind the same opaque-ID
 * convention as `WindowId` / `VirtualScreenId` in PhosphorIdentity:
 * callers pass strings around; only this module knows the format.
 *
 * Identifier shape:
 *   - "Manufacturer:Model:Serial"   (preferred — full EDID identity)
 *   - "Manufacturer:Model"          (fallback when serial is unavailable)
 *   - "Manufacturer:Model:Serial/CONNECTOR"  (duplicate disambiguation)
 *   - connector name (e.g. "DP-2")  (final fallback when EDID is empty)
 *
 * Threading: process-local caches use unsynchronised statics; every
 * function MUST be called from the GUI thread.
 *
 * Process-global state: these helpers intentionally keep their
 * identifier and reverse-lookup caches in function-local statics rather
 * than on a per-ScreenManager object. EDID-to-identifier mapping is
 * determined entirely by the set of connected QScreens (shared
 * QGuiApplication state), so moving the caches onto individual managers
 * would not decouple them — every instance would end up computing the
 * same answers from the same inputs. The trade-off: hosts that run
 * multiple ScreenManager instances (tests, future multi-session shells)
 * share one cache, so `invalidateEdidCache()` on hotplug is a global
 * side-effect. Document rather than isolate because isolation here buys
 * nothing.
 */
namespace ScreenIdentity {

// ─── QScreen-aware helpers ───────────────────────────────────────────────
//
// The EDID parsing + base-ID assembly primitives live in
// PhosphorIdentity::ScreenId (re-included via the header above so callers
// can just write `using namespace PhosphorIdentity::ScreenId;` if they
// want both surfaces).

/**
 * @brief Drop the QScreen-keyed identifier caches AND the underlying
 *        EDID-serial cache (cascades to @ref PhosphorIdentity::ScreenId::invalidateEdidCache).
 *
 * Call on monitor hotplug — the EDID changing means a different monitor
 * is on this connector and every cached identifier referencing it must
 * be rebuilt. Pass an empty string to drop everything.
 */
PHOSPHORSCREENS_EXPORT void invalidateEdidCache(const QString& connectorName = QString());

/**
 * @brief Compute the EDID-based base ID for a `QScreen*`.
 *
 * Identical-monitor disambiguation is NOT applied here (use @ref
 * identifierFor for that). Cached per-`QScreen*` for the screen's
 * lifetime.
 */
PHOSPHORSCREENS_EXPORT QString baseIdentifierFor(const QScreen* screen);

/**
 * @brief Compute the canonical identifier for a `QScreen*`.
 *
 * If another currently-connected screen produces the same base ID
 * (identical monitors on different connectors), appends "/CONNECTOR"
 * to disambiguate. Mirrors KWin's OutputConfigurationStore strategy:
 * EDID primary, connector fallback.
 */
PHOSPHORSCREENS_EXPORT QString identifierFor(const QScreen* screen);

/**
 * @brief Resolve an identifier (connector name OR EDID-style ID, with
 *        or without "/vs:N" virtual-screen suffix) to a `QScreen*`.
 *
 * Priority:
 *   1. Empty input → primary screen.
 *   2. Strip "/vs:N" suffix and resolve the physical parent.
 *   3. Connector name match (fast path).
 *   4. Reverse cache hit.
 *   5. Exact base-ID match against every connected screen.
 *   6. Disambiguated form ("Manuf:Model:Serial/CONNECTOR") match,
 *      verifying both the connector and the EDID base.
 *   7. Legacy: stored config has bare base ID but currently-connected
 *      monitors are duplicates — match the first by base ID.
 *
 * Returns nullptr when no match is found (note: empty input still
 * returns the primary screen).
 */
PHOSPHORSCREENS_EXPORT QScreen* findByIdOrName(const QString& identifier);

/// True if the identifier looks like a connector name (no ':').
inline bool isConnectorName(const QString& identifier)
{
    return !identifier.isEmpty() && !identifier.contains(QLatin1Char(':'));
}

/// Convert a connector name to its current screen identifier.
PHOSPHORSCREENS_EXPORT QString idForName(const QString& connectorName);

/// Convert a screen identifier back to its current connector name.
PHOSPHORSCREENS_EXPORT QString nameForId(const QString& screenId);

/**
 * @brief Tolerance-aware equality between two screen identifiers.
 *
 * Returns true when both inputs resolve to the same `QScreen*`. Handles
 * connector-name ↔ EDID-ID equivalence transparently. Virtual screen
 * IDs are compared exactly; mixed virtual/physical inputs always
 * compare false.
 */
PHOSPHORSCREENS_EXPORT bool screensMatch(const QString& a, const QString& b);

} // namespace ScreenIdentity

} // namespace Phosphor::Screens
