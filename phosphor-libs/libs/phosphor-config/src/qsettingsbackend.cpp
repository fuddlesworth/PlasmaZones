// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorConfig/QSettingsBackend.h>

#include <QFile>
#include <QIODevice>
#include <QTextStream>

namespace PhosphorConfig {

namespace {

// ═══════════════════════════════════════════════════════════════════════════════
// KConfig-compatible INI format
//
// QSettings::IniFormat wraps string values in quotes and escapes internal
// quotes with backslash. KConfig's INI format does NOT quote values. The
// legacy files this reader consumes were written by KConfig-aware consumers,
// so parse standard INI without the quoting convention.
// ═══════════════════════════════════════════════════════════════════════════════

void readKConfigIni(QIODevice& device, QMap<QString, QVariant>& map)
{
    QString currentGroup;
    QTextStream in(&device);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';'))) {
            continue;
        }

        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            currentGroup = line.mid(1, line.length() - 2);
            continue;
        }

        int eqIdx = line.indexOf(QLatin1Char('='));
        if (eqIdx < 0) {
            continue;
        }

        QString key = line.left(eqIdx).trimmed();
        QString value = line.mid(eqIdx + 1); // Preserve leading spaces in values

        QString fullKey = currentGroup.isEmpty() ? key : (currentGroup + QLatin1Char('/') + key);

        // Store typed booleans so toBool() works without re-parsing the string.
        if (value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0) {
            map.insert(fullKey, true);
        } else if (value.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0) {
            map.insert(fullKey, false);
        } else {
            map.insert(fullKey, value);
        }
    }
}

} // namespace

QMap<QString, QVariant> QSettingsBackend::readConfigFromDisk(const QString& filePath)
{
    QMap<QString, QVariant> map;
    QFile f(filePath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        readKConfigIni(f, map);
    }
    return map;
}

} // namespace PhosphorConfig
