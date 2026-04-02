// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configdefaults.h"
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace PlasmaZones {

QString ConfigDefaults::configFilePath()
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (configDir.isEmpty()) {
        // QStandardPaths failed — QDir::homePath() returns "/" when $HOME is unset,
        // so the concatenation is always non-empty.
        configDir = QDir::homePath() + QStringLiteral("/.config");
    }
    return configDir + QStringLiteral("/plasmazonesrc");
}

QString ConfigDefaults::readRenderingBackendFromDisk()
{
    QSettings cfg(configFilePath(), QSettings::IniFormat);

    // QSettings::IniFormat maps keys before any [Section] header into the "General"
    // group automatically on all platforms — so a root-level read (no beginGroup)
    // already resolves "General/RenderingBackend". We also explicitly check the
    // [General] group as a fallback in case the file uses an explicit section header,
    // which is a no-op on Linux but guards against platform-specific IniFormat quirks.
    QString raw = cfg.value(renderingBackendKey()).toString();
    if (raw.isEmpty()) {
        cfg.beginGroup(generalGroup());
        raw = cfg.value(renderingBackendKey(), renderingBackend()).toString();
        cfg.endGroup();
    }
    return normalizeRenderingBackend(raw);
}

} // namespace PlasmaZones
