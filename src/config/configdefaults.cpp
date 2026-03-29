// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configdefaults.h"
#include <QDir>
#include <QStandardPaths>

namespace PlasmaZones {

QString ConfigDefaults::configFilePath()
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (configDir.isEmpty()) {
        configDir = QDir::homePath() + QStringLiteral("/.config");
    }
    if (configDir.isEmpty()) {
        qWarning("ConfigDefaults::configFilePath(): unable to determine config directory");
        return QStringLiteral("/tmp/plasmazonesrc");
    }
    return configDir + QStringLiteral("/plasmazonesrc");
}

} // namespace PlasmaZones
