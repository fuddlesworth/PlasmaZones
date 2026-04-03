// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configdefaults.h"
#include "iconfigbackend.h"
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
    // Try JSON config first — use the default backend factory to stay decoupled
    // from the concrete backend type.
    const QString jsonPath = configFilePath();
    if (QFile::exists(jsonPath)) {
        auto backend = createDefaultConfigBackend();
        auto rendering = backend->group(renderingGroup());
        if (rendering->hasKey(renderingBackendKey())) {
            return normalizeRenderingBackend(rendering->readString(renderingBackendKey(), renderingBackend()));
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
