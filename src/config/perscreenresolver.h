// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorConfig/IGroupPathResolver.h>

namespace PlasmaZones {

/**
 * @brief PhosphorConfig path resolver for PlasmaZones per-screen override groups
 *
 * Maps external group names with a colon-separated prefix (the form
 * "ZoneSelector:EDID-DELL-1234", "AutotileScreen:EDID-DELL-1234", or
 * "SnappingScreen:EDID-DELL-1234") to a nested JSON path rooted at
 * @c "PerScreen/<Category>/<ScreenId>", and enumerates all such groups
 * for @c IBackend::groupList().
 *
 * This resolver keeps the per-screen naming convention out of the
 * PhosphorConfig library. Any code that needs to add a new per-screen
 * category updates @c kPerScreenMappings in the .cpp.
 */
class PLASMAZONES_EXPORT PerScreenPathResolver : public PhosphorConfig::IGroupPathResolver
{
public:
    PerScreenPathResolver();
    ~PerScreenPathResolver() override;

    std::optional<QStringList> toJsonPath(const QString& groupName) const override;
    QStringList reservedRootKeys() const override;
    QStringList enumerate(const QJsonObject& root) const override;

    /// JSON container key under which per-screen groups are stored. Exposed
    /// as a constant so migration code and other callers can reference it
    /// without hardcoding a string literal.
    static const QString& perScreenKey();

    /// Returns true when @p groupName uses one of the known per-screen
    /// prefixes (ZoneSelector:, AutotileScreen:, SnappingScreen:).
    /// Non-matching colon-containing names (e.g. Assignment:*) return false.
    static bool isPerScreenPrefix(const QString& groupName);

    /// Map a prefix (e.g. "AutotileScreen") to its JSON category key
    /// (e.g. "Autotile"). ZoneSelector maps to itself.
    static QString prefixToCategory(const QString& prefix);

    /// Reverse of prefixToCategory: "Autotile" → "AutotileScreen".
    static QString categoryToPrefix(const QString& category);
};

} // namespace PlasmaZones
