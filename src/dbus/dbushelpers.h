// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "../core/interfaces.h"
#include "../core/layout.h"
#include "../core/zone.h"
#include "../core/utils.h"
#include "../core/logging.h"
#include <QScreen>
#include <QUuid>
#include <QString>
#include <optional>

namespace PlasmaZones {

/**
 * @brief Shared D-Bus adaptor helper functions (DRY)
 *
 * Consolidates common validation patterns used across multiple D-Bus adaptors:
 * - UUID parsing and validation
 * - Layout/screen null checking
 * - Zone lookup with proper error handling
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
template <typename LogCategory>
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
// Layout Validation
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Get the active layout with null check and warning
 * @param mgr Layout manager to query
 * @param operation Description for error logging
 * @param category Logging category to use
 * @return Active layout pointer or nullptr (logs warning if null)
 *
 * Consolidates the common pattern:
 *   auto* layout = m_layoutManager->activeLayout();
 *   if (!layout) { qCWarning() << "no active layout"; return; }
 */
template <typename LogCategory>
Layout* getActiveLayoutOrWarn(ILayoutManager* mgr, const QString& operation, LogCategory category)
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
 * @brief Overload using default lcDbus category
 */
inline Layout* getActiveLayoutOrWarn(ILayoutManager* mgr, const QString& operation)
{
    return getActiveLayoutOrWarn(mgr, operation, lcDbus);
}

/**
 * @brief Get a zone by UUID from the active layout
 * @param mgr Layout manager to query
 * @param zoneId Zone UUID string
 * @param operation Description for error logging
 * @param category Logging category to use
 * @return Zone pointer or nullptr (logs appropriate warnings)
 *
 * Combines UUID validation + active layout check + zone lookup.
 */
template <typename LogCategory>
Zone* getZoneFromActiveLayout(ILayoutManager* mgr, const QString& zoneId, const QString& operation, LogCategory category)
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
inline Zone* getZoneFromActiveLayout(ILayoutManager* mgr, const QString& zoneId, const QString& operation)
{
    return getZoneFromActiveLayout(mgr, zoneId, operation, lcDbus);
}

/**
 * @brief Find a zone by UUID across all layouts (not just active)
 * @param mgr Layout manager to search
 * @param zoneId Zone UUID string
 * @param operation Description for error logging
 * @param category Logging category to use
 * @return Zone pointer or nullptr (logs warning if not found)
 *
 * Searches active layout first, then all layouts.
 * Useful for per-screen layout assignments where zone may be in non-active layout.
 */
template <typename LogCategory>
Zone* findZoneInAnyLayout(ILayoutManager* mgr, const QString& zoneId, const QString& operation, LogCategory category)
{
    auto uuidOpt = parseAndValidateUuid(zoneId, operation, category);
    if (!uuidOpt) {
        return nullptr;
    }

    if (!mgr) {
        qCWarning(category) << "Cannot" << operation << "- no layout manager";
        return nullptr;
    }

    QUuid uuid = *uuidOpt;
    Zone* zone = nullptr;

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
inline Zone* findZoneInAnyLayout(ILayoutManager* mgr, const QString& zoneId, const QString& operation)
{
    return findZoneInAnyLayout(mgr, zoneId, operation, lcDbus);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen Validation
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Get a screen by name with null check and warning
 * @param screenName Screen name to find (empty = primary screen)
 * @param operation Description for error logging
 * @param category Logging category to use
 * @return Screen pointer or nullptr (logs warning if not found)
 */
template <typename LogCategory>
QScreen* getScreenOrWarn(const QString& screenName, const QString& operation, LogCategory category)
{
    QScreen* screen = Utils::findScreenByName(screenName);
    if (!screen) {
        qCWarning(category) << operation << ": screen not found:" << screenName;
        return nullptr;
    }
    return screen;
}

/**
 * @brief Overload using default lcDbus category
 */
inline QScreen* getScreenOrWarn(const QString& screenName, const QString& operation)
{
    return getScreenOrWarn(screenName, operation, lcDbus);
}

/**
 * @brief Get primary screen with null check and warning
 * @param operation Description for error logging
 * @param category Logging category to use
 * @return Primary screen pointer or nullptr (logs warning if null)
 */
template <typename LogCategory>
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
template <typename LogCategory>
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

} // namespace DbusHelpers
} // namespace PlasmaZones
