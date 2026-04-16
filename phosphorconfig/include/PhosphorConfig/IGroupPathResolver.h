// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorconfig_export.h>

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace PhosphorConfig {

/// Optional plug-in for custom group name semantics.
///
/// PhosphorConfig's bundled backends understand two group forms out of the box:
///   - Flat names:         "General"
///   - Dot-path names:     "Snapping.Behavior.ZoneSpan"   → nested JSON objects
///
/// Consumers that need additional name conventions (e.g. "ZoneSelector:ScreenId"
/// for per-screen overrides) can register an IGroupPathResolver on the backend.
/// The resolver intercepts group names before they reach storage and maps them
/// to an internal JSON path, and vice versa for enumeration.
///
/// A resolver is queried first. If @c toJsonPath returns @c std::nullopt the
/// backend falls back to the built-in dot-path resolution.
class PHOSPHORCONFIG_EXPORT IGroupPathResolver
{
public:
    virtual ~IGroupPathResolver() = default;

    /// Map an external group name to its JSON path segments.
    /// Return @c std::nullopt to decline — the backend will use dot-path rules.
    /// Return an empty list to signal "malformed, refuse to read or write".
    virtual std::optional<QStringList> toJsonPath(const QString& groupName) const = 0;

    /// Extra top-level JSON keys that this resolver owns. Excluded from the
    /// default dot-path enumeration so they don't appear twice in groupList().
    virtual QStringList reservedRootKeys() const
    {
        return {};
    }

    /// Enumerate every external group name currently represented in the
    /// backing JSON document under this resolver's reserved keys. Results
    /// are appended to IBackend::groupList() output.
    virtual QStringList enumerate(const QJsonObject& root) const
    {
        Q_UNUSED(root);
        return {};
    }

protected:
    IGroupPathResolver() = default;
};

} // namespace PhosphorConfig
