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
        // QStandardPaths failed — QDir::homePath() returns "/" when $HOME is unset,
        // so the concatenation is always non-empty.
        configDir = QDir::homePath() + QStringLiteral("/.config");
    }
    return configDir + QStringLiteral("/plasmazonesrc");
}

} // namespace PlasmaZones
