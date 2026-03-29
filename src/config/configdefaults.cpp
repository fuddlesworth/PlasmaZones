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
        // Both QStandardPaths and QDir::homePath() failed — severely broken environment.
        // Fall back to the runtime directory (user-private on systemd) or /tmp as last resort.
        configDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (configDir.isEmpty())
            configDir = QStringLiteral("/tmp");
        qWarning(
            "ConfigDefaults::configFilePath(): unable to determine config directory, "
            "falling back to %s — config will not persist across reboots",
            qPrintable(configDir));
    }
    return configDir + QStringLiteral("/plasmazonesrc");
}

} // namespace PlasmaZones
