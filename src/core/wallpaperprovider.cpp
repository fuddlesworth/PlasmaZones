// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wallpaperprovider.h"
#include "logging.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace PlasmaZones {

// ── KDE Plasma provider ─────────────────────────────────────────────────────
// Reads ~/.config/plasma-org.kde.plasma.desktop-appletsrc
// Path: [Containments][N][Wallpaper][org.kde.image][General] Image=file:///...

class PlasmaWallpaperProvider : public IWallpaperProvider
{
public:
    QString wallpaperPath() override
    {
        const QString configPath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/plasma-org.kde.plasma.desktop-appletsrc");

        if (!QFile::exists(configPath))
            return {};

        QSettings config(configPath, QSettings::IniFormat);

        // Iterate containment subgroups looking for desktop (formfactor=0)
        const QStringList topGroups = config.childGroups();
        if (!topGroups.contains(QStringLiteral("Containments")))
            return {};

        config.beginGroup(QStringLiteral("Containments"));
        const QStringList containmentIds = config.childGroups();

        for (const QString& id : containmentIds) {
            config.beginGroup(id);
            int formfactor = config.value(QStringLiteral("formfactor"), -1).toInt();
            if (formfactor != 0) {
                config.endGroup();
                continue;
            }
            // Navigate: Wallpaper/org.kde.image/General/Image
            QString imagePath = config.value(QStringLiteral("Wallpaper/org.kde.image/General/Image")).toString();
            config.endGroup();

            if (imagePath.isEmpty())
                continue;

            if (imagePath.startsWith(QLatin1String("file://"))) {
                imagePath = QUrl(imagePath).toLocalFile();
            }
            if (QFile::exists(imagePath)) {
                qCDebug(lcCore) << "Plasma wallpaper:" << imagePath;
                return imagePath;
            }
        }
        config.endGroup();
        return {};
    }
};

// ── Hyprland provider ────────────────────────────────────────────────────────
// Reads ~/.config/hypr/hyprpaper.conf for "wallpaper=" lines,
// or queries swww if available.

class HyprlandWallpaperProvider : public IWallpaperProvider
{
public:
    QString wallpaperPath() override
    {
        // Try swww first (dynamic wallpaper tool)
        QString swwwPath = QStandardPaths::findExecutable(QStringLiteral("swww"));
        if (!swwwPath.isEmpty()) {
            QProcess proc;
            proc.start(swwwPath, {QStringLiteral("query")});
            if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
                // Output format: "monitor: image_path"
                QString output = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
                int colonIdx = output.indexOf(QLatin1Char(':'));
                if (colonIdx > 0) {
                    QString path = output.mid(colonIdx + 1).trimmed();
                    if (QFile::exists(path)) {
                        qCDebug(lcCore) << "swww wallpaper:" << path;
                        return path;
                    }
                }
            }
        }

        // Fall back to hyprpaper.conf
        QString configPath =
            QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/hypr/hyprpaper.conf");
        if (!QFile::exists(configPath))
            return {};

        QFile file(configPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return {};

        while (!file.atEnd()) {
            QString line = QString::fromUtf8(file.readLine()).trimmed();
            // Format: wallpaper = monitor,/path/to/image
            if (line.startsWith(QLatin1String("wallpaper"))) {
                int eqIdx = line.indexOf(QLatin1Char('='));
                if (eqIdx < 0)
                    continue;
                QString value = line.mid(eqIdx + 1).trimmed();
                int commaIdx = value.indexOf(QLatin1Char(','));
                QString path = (commaIdx >= 0) ? value.mid(commaIdx + 1).trimmed() : value;
                if (QFile::exists(path)) {
                    qCDebug(lcCore) << "Hyprpaper wallpaper:" << path;
                    return path;
                }
            }
        }
        return {};
    }
};

// ── Sway/wlroots provider ────────────────────────────────────────────────────
// Queries swaybg or swww for the current wallpaper.

class SwayWallpaperProvider : public IWallpaperProvider
{
public:
    QString wallpaperPath() override
    {
        // swww works on sway too
        QString swwwPath = QStandardPaths::findExecutable(QStringLiteral("swww"));
        if (!swwwPath.isEmpty()) {
            QProcess proc;
            proc.start(swwwPath, {QStringLiteral("query")});
            if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
                QString output = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
                int colonIdx = output.indexOf(QLatin1Char(':'));
                if (colonIdx > 0) {
                    QString path = output.mid(colonIdx + 1).trimmed();
                    if (QFile::exists(path)) {
                        qCDebug(lcCore) << "swww wallpaper (sway):" << path;
                        return path;
                    }
                }
            }
        }
        // swaybg doesn't provide a query interface — no reliable way to read it
        return {};
    }
};

// ── GNOME provider ───────────────────────────────────────────────────────────
// Reads wallpaper via gsettings.

class GnomeWallpaperProvider : public IWallpaperProvider
{
public:
    QString wallpaperPath() override
    {
        QString gsettings = QStandardPaths::findExecutable(QStringLiteral("gsettings"));
        if (gsettings.isEmpty())
            return {};

        QProcess proc;
        proc.start(
            gsettings,
            {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.background"), QStringLiteral("picture-uri")});
        if (!proc.waitForFinished(1000) || proc.exitCode() != 0)
            return {};

        QString uri = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        // gsettings wraps in quotes: 'file:///path/to/image.jpg'
        uri.remove(QLatin1Char('\''));
        if (uri.startsWith(QLatin1String("file://"))) {
            uri = QUrl(uri).toLocalFile();
        }
        if (QFile::exists(uri)) {
            qCDebug(lcCore) << "GNOME wallpaper:" << uri;
            return uri;
        }
        return {};
    }
};

// ── Fallback provider ────────────────────────────────────────────────────────

class NullWallpaperProvider : public IWallpaperProvider
{
public:
    QString wallpaperPath() override
    {
        return {};
    }
};

// ── Factory ──────────────────────────────────────────────────────────────────

std::unique_ptr<IWallpaperProvider> createWallpaperProvider()
{
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    const QString session = qEnvironmentVariable("DESKTOP_SESSION").toLower();

    if (desktop.contains(QLatin1String("kde")) || desktop.contains(QLatin1String("plasma"))
        || session.contains(QLatin1String("plasma"))) {
        return std::make_unique<PlasmaWallpaperProvider>();
    }
    if (desktop.contains(QLatin1String("hyprland")) || session.contains(QLatin1String("hyprland"))
        || !qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE").isEmpty()) {
        return std::make_unique<HyprlandWallpaperProvider>();
    }
    if (desktop.contains(QLatin1String("sway")) || session.contains(QLatin1String("sway"))) {
        return std::make_unique<SwayWallpaperProvider>();
    }
    if (desktop.contains(QLatin1String("gnome")) || desktop.contains(QLatin1String("unity"))) {
        return std::make_unique<GnomeWallpaperProvider>();
    }

    qCDebug(lcCore) << "No wallpaper provider for desktop:" << desktop;
    return std::make_unique<NullWallpaperProvider>();
}

} // namespace PlasmaZones
