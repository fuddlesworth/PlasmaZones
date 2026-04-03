// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"
#include "configbackend_json.h"
#include "configbackend_qsettings.h"
#include "configdefaults.h"
#include <QColor>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLatin1String>

namespace PlasmaZones {

bool ConfigMigration::ensureJsonConfig()
{
    const QString jsonPath = ConfigDefaults::configFilePath();
    if (QFile::exists(jsonPath)) {
        // Verify the file is non-empty and contains valid JSON with data.
        // An empty or corrupt file (e.g. from interrupted write) should not
        // block migration — fall through to re-migrate from INI if available.
        QFile f(jsonPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QByteArray data = f.readAll();
            if (!data.trimmed().isEmpty()) {
                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(data, &err);
                if (err.error == QJsonParseError::NoError && doc.isObject()) {
                    return true; // Already migrated or fresh JSON config (including empty {})
                }
            }
        }
        // Corrupt or empty JSON — check if INI backup exists for re-migration.
        // If no INI exists either, preserve the corrupt file as .corrupt.bak
        // so the user has a recovery path (rather than silently losing data).
        const QString iniPath = ConfigDefaults::legacyConfigFilePath();
        if (!QFile::exists(iniPath)) {
            const QString corruptBak = jsonPath + QStringLiteral(".corrupt.bak");
            if (QFile::exists(corruptBak)) {
                QFile::remove(corruptBak);
            }
            QFile::rename(jsonPath, corruptBak);
            qWarning(
                "ConfigMigration: corrupt JSON config moved to %s — no INI to re-migrate from, "
                "using defaults",
                qPrintable(corruptBak));
            return true;
        }
        // INI exists — remove corrupt JSON so migration can re-create it
        QFile::remove(jsonPath);
    }

    const QString iniPath = ConfigDefaults::legacyConfigFilePath();
    if (!QFile::exists(iniPath)) {
        return true; // Fresh install — no old config to migrate
    }

    qInfo("ConfigMigration: migrating %s → %s", qPrintable(iniPath), qPrintable(jsonPath));

    if (!migrateIniToJson(iniPath, jsonPath)) {
        qWarning("ConfigMigration: migration failed — old config preserved at %s", qPrintable(iniPath));
        return false;
    }

    // Rename old file to .bak
    const QString bakPath = iniPath + QStringLiteral(".bak");
    if (QFile::exists(bakPath)) {
        QFile::remove(bakPath);
    }
    if (!QFile::rename(iniPath, bakPath)) {
        // Non-fatal: migration succeeded, just couldn't rename
        qWarning("ConfigMigration: could not rename %s to %s", qPrintable(iniPath), qPrintable(bakPath));
    }

    qInfo("ConfigMigration: migration complete");
    return true;
}

bool ConfigMigration::migrateIniToJson(const QString& iniPath, const QString& jsonPath)
{
    // Read old INI using the QSettings backend's static reader
    const QMap<QString, QVariant> flatMap = QSettingsConfigBackend::readConfigFromDisk(iniPath);
    if (flatMap.isEmpty()) {
        qInfo("ConfigMigration: old config is empty — writing minimal JSON to complete migration");
    }

    QJsonObject root = iniMapToJson(flatMap);
    // Schema version for future migration steps (e.g. v2 might restructure groups).
    // Currently write-only — checked when a future migration needs to distinguish formats.
    root[QLatin1String("_version")] = 1;

    return JsonConfigBackend::writeJsonAtomically(jsonPath, root);
}

QJsonObject ConfigMigration::iniMapToJson(const QMap<QString, QVariant>& flatMap)
{
    QJsonObject root;

    for (auto it = flatMap.constBegin(); it != flatMap.constEnd(); ++it) {
        const QString& flatKey = it.key();
        const QVariant& value = it.value();

        const int slashIdx = flatKey.indexOf(QLatin1Char('/'));
        if (slashIdx < 0) {
            // Root-level INI key (ungrouped). Route RenderingBackend to its own group.
            if (flatKey == QLatin1String("RenderingBackend")) {
                QJsonObject rendering = root.value(QLatin1String("Rendering")).toObject();
                rendering[flatKey] = convertValue(value);
                root[QLatin1String("Rendering")] = rendering;
            } else {
                QJsonObject general = root.value(QLatin1String("General")).toObject();
                general[flatKey] = convertValue(value);
                root[QLatin1String("General")] = general;
            }
            continue;
        }

        const QString groupPart = flatKey.left(slashIdx);
        const QString keyPart = flatKey.mid(slashIdx + 1);

        // QSettings maps ungrouped keys AND [General]-grouped keys identically,
        // so RenderingBackend may appear as either a root key (handled above) or
        // as "General/RenderingBackend". Route it to the Rendering group either way.
        if (groupPart == QLatin1String("General") && keyPart == QLatin1String("RenderingBackend")) {
            QJsonObject rendering = root.value(QLatin1String("Rendering")).toObject();
            rendering[keyPart] = convertValue(value);
            root[QLatin1String("Rendering")] = rendering;
            continue;
        }

        // Check for known per-screen group patterns: ZoneSelector:*, AutotileScreen:*, SnappingScreen:*
        // Other colon-containing groups (e.g., Assignment:ScreenId:Desktop:1) are regular groups.
        if (JsonConfigBackend::isPerScreenPrefix(groupPart)) {
            const int colonIdx = groupPart.indexOf(QLatin1Char(':'));
            const QString prefix = groupPart.left(colonIdx);
            const QString screenId = groupPart.mid(colonIdx + 1);
            const QString category = JsonConfigBackend::prefixToCategory(prefix);

            QJsonObject perScreen = root.value(QLatin1String(JsonConfigBackend::PerScreenKey)).toObject();
            QJsonObject cat = perScreen.value(category).toObject();
            QJsonObject screen = cat.value(screenId).toObject();
            screen[keyPart] = convertValue(value);
            cat[screenId] = screen;
            perScreen[category] = cat;
            root[QLatin1String(JsonConfigBackend::PerScreenKey)] = perScreen;
        } else {
            // Regular group: Group/Key
            QJsonObject groupObj = root.value(groupPart).toObject();
            groupObj[keyPart] = convertValue(value);
            root[groupPart] = groupObj;
        }
    }

    return root;
}

QJsonValue ConfigMigration::convertValue(const QVariant& value)
{
    const QString s = value.toString();

    // Already a typed bool from INI reader
    if (value.typeId() == QMetaType::Bool) {
        return QJsonValue(value.toBool());
    }

    // Try to detect and convert value types

    // Boolean strings
    if (s.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0) {
        return QJsonValue(true);
    }
    if (s.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0) {
        return QJsonValue(false);
    }

    // Try JSON (triggers, per-algorithm settings)
    if (!s.isEmpty() && (s.front() == QLatin1Char('[') || s.front() == QLatin1Char('{'))) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError) {
            if (doc.isArray()) {
                return QJsonValue(doc.array());
            }
            if (doc.isObject()) {
                return QJsonValue(doc.object());
            }
        }
    }

    // Color in r,g,b or r,g,b,a format → convert to hex
    if (s.contains(QLatin1Char(','))) {
        const QStringList parts = s.split(QLatin1Char(','));
        if (parts.size() >= 3 && parts.size() <= 4) {
            bool allNumeric = true;
            QList<int> components;
            for (const QString& part : parts) {
                bool ok = false;
                int v = part.trimmed().toInt(&ok);
                if (!ok || v < 0 || v > 255) {
                    allNumeric = false;
                    break;
                }
                components.append(v);
            }
            if (allNumeric && components.size() >= 3) {
                int a = (components.size() >= 4) ? components[3] : 255;
                QColor c(components[0], components[1], components[2], a);
                return QJsonValue(c.name(QColor::HexArgb));
            }
        }
        // Not a color — might be a comma-separated list, keep as string
        return QJsonValue(s);
    }

    // Try integer
    {
        bool ok = false;
        int intVal = s.toInt(&ok);
        if (ok) {
            return QJsonValue(intVal);
        }
    }

    // Try double (but only if it has a decimal point to avoid converting "0" → 0.0)
    if (s.contains(QLatin1Char('.'))) {
        bool ok = false;
        double dblVal = s.toDouble(&ok);
        if (ok) {
            return QJsonValue(dblVal);
        }
    }

    // Default: keep as string
    return QJsonValue(s);
}

} // namespace PlasmaZones
