// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configdefaults.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>

namespace PlasmaZones {

static QString configDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (dir.isEmpty()) {
        dir = QDir::homePath() + QStringLiteral("/.config");
    }
    return dir;
}

QString ConfigDefaults::configFilePath()
{
    return configDir() + QStringLiteral("/plasmazones/config.json");
}

QString ConfigDefaults::legacyConfigFilePath()
{
    return configDir() + QStringLiteral("/plasmazonesrc");
}

QString ConfigDefaults::readRenderingBackendFromDisk()
{
    // Lightweight read — only needs one key, so avoid constructing a full backend
    // (which parses the entire file and tracks group state).  This runs before
    // QCoreApplication exists on the daemon startup path.
    const QString jsonPath = configFilePath();
    if (QFile::exists(jsonPath)) {
        QFile f(jsonPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject rendering = doc.object().value(renderingGroup()).toObject();
                if (rendering.contains(renderingBackendKey())) {
                    return normalizeRenderingBackend(
                        rendering.value(renderingBackendKey()).toString(renderingBackend()));
                }
            }
        }
    }

    // Fallback: read from legacy INI file if JSON doesn't exist yet (migration
    // hasn't run — this function is called very early, before ensureJsonConfig()).
    const QString iniPath = legacyConfigFilePath();
    if (QFile::exists(iniPath)) {
        QSettings cfg(iniPath, QSettings::IniFormat);
        const QString raw = cfg.value(renderingBackendKey(), renderingBackend()).toString();
        return normalizeRenderingBackend(raw);
    }

    return renderingBackend();
}

} // namespace PlasmaZones
