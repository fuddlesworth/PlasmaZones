// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include "core/interfaces/interfaces.h"
#include "core/platform/logging.h"
#include "core/utils/utils.h"

#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>

#include <QDBusError>
#include <QRectF>
#include <QScreen>
#include <QString>
#include <QUuid>

#include <optional>

class QDBusContext;

namespace PhosphorScreens {
class ScreenManager;
}

namespace PlasmaZones {

/**
 * @brief Shared D-Bus adaptor helper functions
 *
 * Consolidates common validation patterns used across multiple D-Bus adaptors:
 * - UUID parsing and validation
 * - PhosphorZones::Layout/screen null checking
 * - PhosphorZones::Zone lookup with proper error handling
 *
 * Design rationale: These helpers use template logging categories to allow
 * each adaptor to log under its own category while sharing the validation logic.
 */
namespace DbusHelpers {

// ═══════════════════════════════════════════════════════════════════════════════
// UUID Validation
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Parse and validate a UUID string with logging
 * @param id UUID string to parse
 * @param operation Description for error logging (e.g., "highlightZone")
 * @param category Logging category to use
 * @return Valid QUuid or std::nullopt on failure (logs warning)
 *
 * Consolidates the common pattern:
 *   if (id.isEmpty()) { qCWarning() << "..."; return; }
 *   auto uuid = Utils::parseUuid(id);
 *   if (!uuid) { qCWarning() << "..."; return; }
 */
template<typename LogCategory>
std::optional<QUuid> parseAndValidateUuid(const QString& id, const QString& operation, LogCategory category)
{
    if (id.isEmpty()) {
        qCWarning(category) << "Cannot" << operation << "- empty ID";
        return std::nullopt;
    }

    auto uuidOpt = Utils::parseUuid(id);
    if (!uuidOpt) {
        qCWarning(category) << "Invalid UUID format for" << operation << ":" << id;
        return std::nullopt;
    }

    return uuidOpt;
}

/**
 * @brief Overload using default lcDbus category
 */
inline std::optional<QUuid> parseAndValidateUuid(const QString& id, const QString& operation)
{
    return parseAndValidateUuid(id, operation, lcDbus);
}

// ═══════════════════════════════════════════════════════════════════════════════
// PhosphorZones::Layout Validation
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Get the active layout with null check and warning
 * @param mgr PhosphorZones::Layout manager to query
 * @param operation Description for error logging
 * @param category Logging category to use
 * @return Active layout pointer or nullptr (logs warning if null)
 *
 * Consolidates the common pattern:
 *   auto* layout = m_layoutManager->activeLayout();
 *   if (!layout) { qCWarning() << "no active layout"; return; }
 */
template<typename LogCategory>
PhosphorZones::Layout* getActiveLayoutOrWarn(PhosphorZones::IZoneLayoutRegistry* mgr, const QString& operation,
                                             LogCategory category)
{
    if (!mgr) {
        qCWarning(category) << "Cannot" << operation << "- no layout manager";
        return nullptr;
    }

    auto* layout = mgr->activeLayout();
    if (!layout) {
        qCWarning(category) << "Cannot" << operation << "- no active layout";
        return nullptr;
    }

    return layout;
}

/**
 * @brief Get a zone by UUID from the active layout
 * @param mgr PhosphorZones::Layout manager to query
 * @param zoneId PhosphorZones::Zone UUID string
 * @param operation Description for error logging
 * @param category Logging category to use
 * @return PhosphorZones::Zone pointer or nullptr (logs appropriate warnings)
 *
 * Combines UUID validation + active layout check + zone lookup.
 */
template<typename LogCategory>
PhosphorZones::Zone* getZoneFromActiveLayout(PhosphorZones::IZoneLayoutRegistry* mgr, const QString& zoneId,
                                             const QString& operation, LogCategory category)
{
    auto uuidOpt = parseAndValidateUuid(zoneId, operation, category);
    if (!uuidOpt) {
        return nullptr;
    }

    auto* layout = getActiveLayoutOrWarn(mgr, operation, category);
    if (!layout) {
        return nullptr;
    }

    auto* zone = layout->zoneById(*uuidOpt);
    if (!zone) {
        qCWarning(category) << "Zone not found for" << operation << ":" << zoneId;
        return nullptr;
    }

    return zone;
}

/**
 * @brief Overload using default lcDbus category
 */
inline PhosphorZones::Zone* getZoneFromActiveLayout(PhosphorZones::IZoneLayoutRegistry* mgr, const QString& zoneId,
                                                    const QString& operation)
{
    return getZoneFromActiveLayout(mgr, zoneId, operation, lcDbus);
}

/**
 * @brief Find a zone by UUID across all layouts (not just active)
 * @param mgr PhosphorZones::Layout manager to search
 * @param zoneId PhosphorZones::Zone UUID string
 * @param operation Description for error logging
 * @param category Logging category to use
 * @return PhosphorZones::Zone pointer or nullptr (logs warning if not found)
 *
 * Searches active layout first, then all layouts.
 * Useful for per-screen layout assignments where zone may be in non-active layout.
 *
 * @note O(N×M) in (layouts, zones-per-layout) on the cold path. Acceptable
 * for the per-screen-assignment fallback use case where N is small (a few
 * layouts) and only triggers when the active layout misses; not suitable
 * for hot-path zone lookups (use the IZoneLayoutRegistry by-id surface).
 */
template<typename LogCategory>
PhosphorZones::Zone* findZoneInAnyLayout(PhosphorZones::IZoneLayoutRegistry* mgr, const QString& zoneId,
                                         const QString& operation, LogCategory category)
{
    // `activeLayout()` and `layouts()` are both on the interface - no
    // need to require the concrete registry just to reach them.
    auto uuidOpt = parseAndValidateUuid(zoneId, operation, category);
    if (!uuidOpt) {
        return nullptr;
    }

    if (!mgr) {
        qCWarning(category) << "Cannot" << operation << "- no layout manager";
        return nullptr;
    }

    QUuid uuid = *uuidOpt;
    PhosphorZones::Zone* zone = nullptr;

    // First try active layout
    if (auto* activeLayout = mgr->activeLayout()) {
        zone = activeLayout->zoneById(uuid);
    }

    // If not found, search all layouts
    if (!zone) {
        for (auto* layout : mgr->layouts()) {
            zone = layout->zoneById(uuid);
            if (zone) {
                break;
            }
        }
    }

    if (!zone) {
        qCWarning(category) << "Zone not found in any layout for" << operation << ":" << zoneId;
    }

    return zone;
}

/**
 * @brief Overload using default lcDbus category
 */
inline PhosphorZones::Zone* findZoneInAnyLayout(PhosphorZones::IZoneLayoutRegistry* mgr, const QString& zoneId,
                                                const QString& operation)
{
    return findZoneInAnyLayout(mgr, zoneId, operation, lcDbus);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen Validation
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Get primary screen with null check and warning
 * @param operation Description for error logging
 * @param category Logging category to use
 * @return Primary screen pointer or nullptr (logs warning if null)
 */
template<typename LogCategory>
QScreen* getPrimaryScreenOrWarn(const QString& operation, LogCategory category)
{
    QScreen* screen = Utils::primaryScreen();
    if (!screen) {
        qCWarning(category) << operation << ": no primary screen";
        return nullptr;
    }
    return screen;
}

/**
 * @brief Overload using default lcDbus category
 */
inline QScreen* getPrimaryScreenOrWarn(const QString& operation)
{
    return getPrimaryScreenOrWarn(operation, lcDbus);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Validation Helpers
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Validate that a required string parameter is not empty
 * @param value The value to check
 * @param paramName Parameter name for error message (e.g., "zone ID")
 * @param operation Operation name for error message
 * @param category Logging category to use
 * @return true if valid (non-empty), false if empty (logs warning)
 */
template<typename LogCategory>
bool validateNonEmpty(const QString& value, const QString& paramName, const QString& operation, LogCategory category)
{
    if (value.isEmpty()) {
        qCWarning(category) << "Cannot" << operation << "- empty" << paramName;
        return false;
    }
    return true;
}

/**
 * @brief Overload using default lcDbus category
 */
inline bool validateNonEmpty(const QString& value, const QString& paramName, const QString& operation)
{
    return validateNonEmpty(value, paramName, operation, lcDbus);
}

/**
 * @brief Validate a 1-based virtual desktop number arriving over the bus.
 *
 * Desktops are 1-based everywhere in the assignment API; 0 is the wildcard
 * meaning "any desktop" and is only legal on the slots documented to accept it,
 * which pass @p allowZero. A negative desktop names a context nothing can ever
 * resolve — persisting an assignment rule for it is a silent leak that also
 * grows the rule set without bound under repeated calls. The lower bound is the
 * whole guard: this helper has no view of the compositor's desktop count, so a
 * too-high desktop passes here and is left to the caller.
 *
 * @param virtualDesktop The desktop number as received.
 * @param operation Operation name for the warning.
 * @param allowZero Accept 0 as the screen-level wildcard.
 * @return true when the value is usable, false after logging a warning.
 */
template<typename LogCategory>
bool validateDesktopNumber(int virtualDesktop, const QString& operation, LogCategory category, bool allowZero = false)
{
    const int minimum = allowZero ? 0 : 1;
    if (virtualDesktop < minimum) {
        qCWarning(category) << "Cannot" << operation << "- virtual desktop" << virtualDesktop << "is below" << minimum;
        return false;
    }
    return true;
}

/**
 * @brief Overload using default lcDbus category
 */
inline bool validateDesktopNumber(int virtualDesktop, const QString& operation, bool allowZero = false)
{
    return validateDesktopNumber(virtualDesktop, operation, lcDbus, allowZero);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen ID Resolution (implemented in dbushelpers.cpp to reduce header weight)
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Resolve a screen ID to an effective (virtual-aware) screen ID
 *
 * When screenId is empty, resolves to the primary screen, or to its first
 * virtual screen when it is subdivided. The cursor is never read: a Wayland
 * background daemon's cursor position is stale. When non-empty, passes virtual
 * screen IDs through and resolves physical names via
 * PhosphorScreens::ScreenIdentity::idForName.
 */
QString resolveScreenId(PhosphorScreens::ScreenManager* mgr, const QString& screenId);

/**
 * @brief Resolve a screen ID (physical or virtual) to its backing QScreen*
 *
 * Uses PhosphorScreens::ScreenManager::physicalScreenFor() when available, then falls back to
 * PhosphorScreens::ScreenIdentity::findByIdOrName(). Does NOT fall back to primaryScreen - returns
 * nullptr so the caller can decide the appropriate fallback behavior.
 */
QScreen* resolvePhysicalQScreen(PhosphorScreens::ScreenManager* mgr, const QString& screenId);

// ═══════════════════════════════════════════════════════════════════════════════
// Screen Geometry Resolution (implemented in dbushelpers.cpp to reduce header weight)
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Resolve effective screen geometry for a layout, handling virtual screens
 *
 * Prefers PhosphorScreens::ScreenManager geometry (virtual-screen-aware) when available,
 * falls back to resolving the physical QScreen and using the QScreen overload.
 * A virtual screen id the manager does not resolve is never widened to its
 * physical monitor. Returns an invalid QRectF if no geometry can be resolved.
 */
QRectF resolveScreenGeometry(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Layout* layout,
                             const QString& screenId);

// ═══════════════════════════════════════════════════════════════════════════════
// Bus context (implemented in dbushelpers.cpp)
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief The context of the bus message the object @p adaptor is exported
 *        through is serving now, or null while it serves none (a direct
 *        call, as the unit tests make).
 *
 * That message is whatever the host is delivering: a call to this adaptor or
 * to another one on the same host, or a bus signal delivered to a host slot,
 * so code reached from any of those reads it, not only the code of this
 * adaptor's own slot. The context's connection() is the connection that
 * message came in on.
 *
 * Qt sets a call's QDBusContext on the object the adaptor is exported
 * through, its parent, never on the QDBusAbstractAdaptor itself (see the
 * QDBusContext documentation), so an adaptor that inherits QDBusContext reads
 * calledFromDBus() as false on every call and its sendErrorReply never
 * reaches anyone. The parent has to inherit it (Daemon does); a parent that
 * does not answers null.
 */
PLASMAZONES_EXPORT const QDBusContext* busContext(const QObject* adaptor);

/**
 * @brief The unique bus name of the sender of the message busContext()
 *        reads, or empty while the host serves none.
 */
PLASMAZONES_EXPORT QString busCaller(const QObject* adaptor);

/**
 * @brief The unique bus name of the sender of the call @p adaptor is serving,
 *        when that call is @p member on @p adaptor's own D-Bus interface (or
 *        names none, which Qt dispatches by member name); nullopt otherwise.
 *
 * Nullopt for a direct call, and for a slot reached in-process while the host
 * serves another message: that message's sender is not this slot's caller.
 * A served call can still answer an empty name (a peer-to-peer connection
 * names no sender), which matches no registered bridge.
 */
PLASMAZONES_EXPORT std::optional<QString> busCallerOf(const QObject* adaptor, QLatin1StringView member);

/**
 * @brief Whether @p busName is the unique bus name the compositor bridge
 *        registered from, read from the CompositorBridgeAdaptor on
 *        @p adaptor's host.
 *
 * The one test of "the caller is the registered bridge": admitCompositorReport
 * applies it to compositor-only slots, and the daemon hands it to the adaptors
 * that treat the bridge's calls differently from other callers'.
 */
PLASMAZONES_EXPORT bool isBridgeCaller(const QObject* adaptor, const QString& busName);

/**
 * @brief Answer the bus call @p adaptor is serving with the error @p type,
 *        and report whether a reply went out.
 *
 * Only a method call addressed to @p adaptor's own D-Bus interface is
 * answered, or one naming no interface, which Qt dispatches by member name.
 * While a direct C++ call reaches this adaptor, the host may be serving a
 * call to another of its adaptors or delivering a bus signal, and neither is
 * this adaptor's to answer.
 *
 * On true Qt marks the reply delayed and discards whatever the slot returns,
 * so the slot's return value matters only on false, where it is the answer:
 * to a direct call, or to a bus call this adaptor does not own.
 */
PLASMAZONES_EXPORT bool sendBusError(const QObject* adaptor, QDBusError::ErrorType type, const QString& message);

/**
 * @brief The first statement of a compositor-only slot (#1155): true when the
 *        slot may proceed.
 *
 * A compositor-only method reports what the compositor saw, and the engines
 * take it as ground truth, so only the registered compositor bridge may call
 * it over the bus: the unique bus name CompositorBridgeAdaptor recorded at
 * registerBridge, read from the bridge adaptor on @p adaptor's host so there
 * is one record of it. A bus caller with any other name, or any caller before
 * a bridge registered over the bus, is answered with AccessDenied and the slot
 * returns at once.
 *
 * Admitted without a check: a direct C++ call (no bus context, as the unit
 * tests and the daemon's own calls make), and a call reached while the host
 * serves a different message, which is the case when another slot calls this
 * one (endDrag calling windowScreenChanged, say). That outer slot answers for
 * its own caller. So the check runs only when the message being served is a
 * method call of @p member on @p adaptor's own interface, or one naming no
 * interface, which Qt dispatches by member name (busCallerOf).
 *
 * The identity is a bus connection, not a component: everything that shares
 * the compositor's connection passes too. Under KWin that is every effect and
 * every KWin script, since they all call through KWin's one session-bus
 * connection. Registration is held to the compositor's process
 * (CompositorBridgeAdaptor::setCompositorIdentityCheck), so a peer outside it
 * never passes.
 *
 * A refused caller is logged once at warning, for a bounded number of
 * distinct callers, and at debug after that.
 */
PLASMAZONES_EXPORT bool admitCompositorReport(const QObject* adaptor, QLatin1StringView member);

} // namespace DbusHelpers
} // namespace PlasmaZones
