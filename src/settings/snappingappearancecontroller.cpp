// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snappingappearancecontroller.h"

#include "../config/configdefaults.h"
#include "../config/settings.h"
#include "../pz_i18n.h"

#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace PlasmaZones {

SnappingAppearanceController::SnappingAppearanceController(Settings* settings, QObject* parent)
    : PhosphorSettingsUi::PageController(QStringLiteral("snapping-appearance"), parent)
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
    // QML-callable entry point — `filePath` is untrusted input. Reject
    // empty / non-existent / non-regular / non-.json paths before
    // forwarding to Settings, which does JSON parsing without
    // re-validating the path itself. Defence-in-depth per CLAUDE.md
    // "Sanitize file paths to prevent directory traversal" — the colors
    // file picker should already constrain selection, but the QML
    // surface is a public API.
    const QFileInfo info(filePath);
    if (filePath.isEmpty() || !info.exists() || !info.isFile()) {
        Q_EMIT colorImportError(PzI18n::tr("Color import file does not exist: %1").arg(filePath));
        return;
    }
    if (info.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) != 0) {
        Q_EMIT colorImportError(PzI18n::tr("Color import file must be a .json file: %1").arg(filePath));
        return;
    }

    const QString error = m_settings->loadColorsFromFile(info.canonicalFilePath());
    if (!error.isEmpty()) {
        Q_EMIT colorImportError(error);
        return;
    }

    Q_EMIT colorImportSuccess();
    Q_EMIT changed();
}

} // namespace PlasmaZones
