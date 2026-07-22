// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snappingzonescontroller.h"

#include "config/configdefaults.h"
#include "core/interfaces/isettings.h"
#include "phosphor_i18n.h"

#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace PlasmaZones {

SnappingZonesController::SnappingZonesController(ISettings& settings, QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("snapping-zones"), parent)
    , m_settings(&settings)
{
}

int SnappingZonesController::borderWidthMin() const
{
    return ConfigDefaults::borderWidthMin();
}

int SnappingZonesController::borderWidthMax() const
{
    return ConfigDefaults::borderWidthMax();
}

int SnappingZonesController::borderRadiusMin() const
{
    return ConfigDefaults::borderRadiusMin();
}

int SnappingZonesController::borderRadiusMax() const
{
    return ConfigDefaults::borderRadiusMax();
}

double SnappingZonesController::labelFontScaleMin() const
{
    return ConfigDefaults::labelFontSizeScaleMin();
}

double SnappingZonesController::labelFontScaleMax() const
{
    return ConfigDefaults::labelFontSizeScaleMax();
}

void SnappingZonesController::loadColorsFromPywal()
{
    // Honour $XDG_CACHE_HOME via QStandardPaths rather than hardcoding
    // ~/.cache/wal — pywal itself follows XDG, so hardcoded ~/.cache skips
    // users who've relocated their cache dir.
    const QString pywalPath =
        QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/wal/colors.json");
    // Route through the same validated entry point loadColorsFromFile
    // uses so the defence-in-depth (canonical-path + suffix +
    // regular-file check) applies symmetrically. A user-controllable
    // symlink at ~/.cache/wal/colors.json would otherwise bypass
    // those checks if we forwarded directly to m_settings.
    loadColorsFromFile(pywalPath);
}

void SnappingZonesController::loadColorsFromFile(const QString& filePath)
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
        Q_EMIT colorImportError(PhosphorI18n::tr("Color import file does not exist: %1").arg(filePath));
        return;
    }
    // A symlinked .json colors file (e.g. ~/.cache/wal/colors.json pointing
    // into a versioned config repo, or a user's themed dotfile layout) is
    // a legitimate selection — pywal itself ships symlink layouts. Validate
    // the CANONICAL target instead: it must exist, be a regular file, and
    // have a .json suffix on both the user-visible name and the resolved
    // target. This catches the directory-traversal concern (the resolved
    // target must be a real .json) without rejecting symlinked-colors-file
    // layouts that loadColorsFromPywal() above silently accepts.
    // canonicalFilePath() returns empty for a broken symlink or a path that vanished
    // between the exists() check above and here (TOCTOU) — resolve it ONCE and test
    // that result, rather than re-canonicalizing an already-canonical QFileInfo.
    const QString canonicalPath = info.canonicalFilePath();
    const QFileInfo canonInfo(canonicalPath);
    if (canonicalPath.isEmpty() || !canonInfo.exists() || !canonInfo.isFile()) {
        Q_EMIT colorImportError(
            PhosphorI18n::tr("Color import file does not resolve to a regular file: %1").arg(filePath));
        return;
    }
    if (info.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) != 0
        || canonInfo.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) != 0) {
        Q_EMIT colorImportError(PhosphorI18n::tr("Color import file must be a .json file: %1").arg(filePath));
        return;
    }

    const QString error = m_settings->loadColorsFromFile(canonInfo.absoluteFilePath());
    if (!error.isEmpty()) {
        Q_EMIT colorImportError(error);
        return;
    }

    Q_EMIT colorImportSuccess();
    Q_EMIT changed();
}

} // namespace PlasmaZones
