// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorconfig_export.h>

#include <QMap>
#include <QString>
#include <QVariant>

namespace PhosphorConfig {

/// Reader for legacy KConfig-style INI configuration files.
///
/// Read-only and one-way: it exists so a consumer migrating an old INI file
/// onto @c JsonBackend can get at the old values. There is deliberately no
/// live INI @c IBackend to pair with it — @c QSettings flushes unsaved
/// changes when it is destroyed and offers no way to drop them, so it cannot
/// satisfy @c IBackend::reparseConfiguration's contract to discard pending
/// in-memory changes. An implementation over it would persist exactly the
/// writes callers asked it to throw away.
class PHOSPHORCONFIG_EXPORT QSettingsBackend
{
public:
    /// Read an INI config from an arbitrary file path. Returns a flat QMap
    /// keyed by "Group/Key"; ungrouped keys appear unprefixed.
    ///
    /// Values spelling "true"/"false" (case-insensitively) come back as bools,
    /// everything else as a QString — callers coerce from there. A missing or
    /// unreadable file yields an empty map rather than an error: to a migration
    /// chain, "no legacy config" and "empty legacy config" mean the same thing.
    ///
    /// Parses the file directly rather than through @c QSettings, so no
    /// @c QConfFile cache entry is installed for @p filePath and a caller that
    /// renames or deletes the file afterwards sees no stale cached copy.
    static QMap<QString, QVariant> readConfigFromDisk(const QString& filePath);
};

} // namespace PhosphorConfig
