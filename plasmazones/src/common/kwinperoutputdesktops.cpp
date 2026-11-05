// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/kwinperoutputdesktops.h"

#include <QLatin1Char>
#include <QLatin1String>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace PlasmaZones {

bool kwinPerOutputVirtualDesktopsEnabled()
{
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kwinrc");
    const QSettings kwinrc(path, QSettings::IniFormat);

    const QString group = QStringLiteral("Windows/");
    const QString base = QStringLiteral("PerOutputVirtualDesktops");

    QVariant raw = kwinrc.value(group + base);
    if (!raw.isValid()) {
        // Scan for the same key carrying any KConfig marker suffix. QSettings
        // keeps the bracketed marker as part of the key name, so the exact
        // lookup above cannot find it.
        const QStringList keys = kwinrc.allKeys();
        for (const QString& key : keys) {
            if (key.startsWith(group + base + QLatin1Char('['))) {
                raw = kwinrc.value(key);
                break;
            }
        }
    }
    if (!raw.isValid()) {
        return false;
    }

    const QString text = raw.toString().trimmed();
    return text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
        || text.compare(QLatin1String("on"), Qt::CaseInsensitive) == 0
        || text.compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0 || text == QLatin1String("1");
}

} // namespace PlasmaZones
