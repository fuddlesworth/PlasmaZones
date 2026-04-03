// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// One-time config migration from INI (plasmazonesrc) to JSON (config.json).
// Runs on first startup after upgrade.  Old file is renamed to .bak.

#pragma once

#include "plasmazones_export.h"
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVariant>

namespace PlasmaZones {

class PLASMAZONES_EXPORT ConfigMigration
{
public:
    /// Run the migration if needed.
    ///
    /// Returns true if:
    /// - config.json already exists (no migration needed), or
    /// - No old plasmazonesrc exists (fresh install), or
    /// - Migration succeeded (JSON written, old file renamed to .bak)
    ///
    /// Returns false if migration failed (old file preserved, error logged).
    static bool ensureJsonConfig();

    /// Convert an INI config file to JSON format. Used by ensureJsonConfig()
    /// for one-time migration, and by settings import for legacy INI files.
    static bool migrateIniToJson(const QString& iniPath, const QString& jsonPath);

private:
    ConfigMigration() = default;
    static QJsonObject iniMapToJson(const QMap<QString, QVariant>& flatMap);
    static QJsonValue convertValue(const QVariant& value);
};

} // namespace PlasmaZones
