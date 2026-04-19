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
 * @brief Unconditionally drop every cache this namespace holds (local
 *        identifier caches AND the cross-process EDID serial cache).
 *
 * Test-isolation hook. Production callers almost always want
 * @ref invalidateEdidCache — this function exists so a test harness that
 * recreates @c QGuiApplication within one process can wipe stale pointer-
 * keyed entries before the next run populates them with addresses that
 * happen to match a freed QScreen from a previous iteration.
 */
PHOSPHORSCREENS_EXPORT void reset();

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
 * @brief Drop the computed-identifier and reverse-lookup caches without
 *        touching the EDID-serial cache.
 *
 * Call on QScreen add/remove: identifier disambiguation depends on the
 * full set of connected screens (a "/CONNECTOR" suffix appears only when
 * another screen produces the same base ID), so any topology change can
 * promote a previously-bare ID to a disambiguated form or vice versa.
 * Pruning by connector name (as @ref invalidateEdidCache does) misses
 * this: screen A's cache entry is keyed on its own connector, but the
 * disambiguation of A's identifier depends on whether screen B exists —
 * a delta to B alone leaves A's entry stale.
 *
 * Distinct from @ref invalidateEdidCache because the EDID serial is a
 * hardware property of a specific connector (only changes on physical
 * monitor swap), while disambiguation is a function of the screen set
 * (changes on every add/remove regardless of hardware reuse).
 */
PHOSPHORSCREENS_EXPORT void invalidateComputedIdentifiers();

/**
 * @brief Compute the EDID-based base ID for a `QScreen*`.
 *
 * Identical-monitor disambiguation is NOT applied here (use @ref
 * identifierFor for that). Not cached at this level — the underlying
 * EDID-serial sysfs read IS cached inside
 * @ref PhosphorIdentity::ScreenId::readEdidHeaderSerial, which is what
 * dominates the cost; assembling the "manuf:model:serial" string from
 * already-fetched QScreen fields is cheap enough that a second cache
 * layer would trade memory for negligible speedup.
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
