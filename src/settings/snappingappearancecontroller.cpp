// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snappingappearancecontroller.h"

#include "../config/configdefaults.h"
#include "../config/settings.h"
#include "../pz_i18n.h"

#include <QFile>
#include <QStandardPaths>
#include <QStringLiteral>

namespace PlasmaZones {

SnappingAppearanceController::SnappingAppearanceController(Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    Q_ASSERT(m_settings);
}

int SnappingAppearanceController::borderWidthMin() const
{
    return ConfigDefaults::borderWidthMin();
}

int SnappingAppearanceController::borderWidthMax() const
{
    return ConfigDefaults::borderWidthMax();
}

int SnappingAppearanceController::borderRadiusMin() const
{
    return ConfigDefaults::borderRadiusMin();
}

int SnappingAppearanceController::borderRadiusMax() const
{
    return ConfigDefaults::borderRadiusMax();
}

void SnappingAppearanceController::loadColorsFromPywal()
{
    // Honour $XDG_CACHE_HOME via QStandardPaths rather than hardcoding
    // ~/.cache/wal — pywal itself follows XDG, so hardcoded ~/.cache skips
    // users who've relocated their cache dir.
    const QString pywalPath =
        QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/wal/colors.json");
    if (!QFile::exists(pywalPath)) {
        Q_EMIT colorImportError(PzI18n::tr("Pywal colors not found. Run 'wal' to generate colors first.\n\n"
                                           "Expected file: %1")
                                    .arg(pywalPath));
        return;
    }

    const QString error = m_settings->loadColorsFromFile(pywalPath);
    if (!error.isEmpty()) {
        Q_EMIT colorImportError(error);
        return;
    }

    Q_EMIT colorImportSuccess();
    Q_EMIT changed();
}

void SnappingAppearanceController::loadColorsFromFile(const QString& filePath)
{
    const QString error = m_settings->loadColorsFromFile(filePath);
    if (!error.isEmpty()) {
        Q_EMIT colorImportError(error);
        return;
    }

    Q_EMIT colorImportSuccess();
    Q_EMIT changed();
}

} // namespace PlasmaZones
