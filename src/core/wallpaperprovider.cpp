// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wallpaperprovider.h"
#include "logging.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

namespace PlasmaZones {

namespace {

/// Query swww for the current wallpaper path.
/// Output format: "eDP-1:       /path/to/image.png" (one line per monitor).
/// Returns the first valid path found, or empty string.
QString querySwww()
{
    QString swwwPath = QStandardPaths::findExecutable(QStringLiteral("swww"));
    if (swwwPath.isEmpty())
        return {};

    QProcess proc;
    proc.start(swwwPath, {QStringLiteral("query")});
    if (!proc.waitForFinished(1000) || proc.exitCode() != 0)
        return {};

    const QStringList lines = QString::fromUtf8(proc.readAllStandardOutput()).split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        int colonIdx = line.indexOf(QLatin1Char(':'));
        if (colonIdx <= 0)
            continue;
        QString path = line.mid(colonIdx + 1).trimmed();
        if (!path.isEmpty() && QFile::exists(path)) {
            return path;
        }
    }
    return {};
}

} // anonymous namespace

// ── KDE Plasma provider ─────────────────────────────────────────────────────
// Reads ~/.config/plasma-org.kde.plasma.desktop-appletsrc
// Path: [Containments][N][Wallpaper][org.kde.image][General] Image=file:///...
//
// KConfig uses multi-bracket group headers like [Containments][355] to
// represent nested groups.  QSettings::IniFormat does NOT understand this
// nesting — it treats the entire bracket sequence as a flat group name.
// We parse the file manually to handle KConfig's format correctly.

class PlasmaWallpaperProvider : public IWallpaperProvider
{
public:
    QString wallpaperPath() override
    {
        const QString configPath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/plasma-org.kde.plasma.desktop-appletsrc");

        QFile file(configPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return {};

        // Two-pass approach:
        // 1. Find containment IDs with formfactor=0 (desktop containments)
        // 2. Find Image= under matching [Containments][N][Wallpaper][org.kde.image][General]
        //
        // KConfig format: group headers are [A][B][C], keys are Key=Value under them.

        QSet<QString> desktopContainmentIds;
        QString currentGroup;

        // Pass 1: find desktop containments
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;

            if (line.startsWith(QLatin1Char('['))) {
                currentGroup = line;
                continue;
            }

            if (line.startsWith(QLatin1String("formfactor=0"))) {
                // Check if current group matches [Containments][NNN] (exactly 2 brackets)
                static const QRegularExpression re(QStringLiteral("^\\[Containments\\]\\[(\\d+)\\]$"));
                QRegularExpressionMatch match = re.match(currentGroup);
                if (match.hasMatch()) {
                    desktopContainmentIds.insert(match.captured(1));
                }
            }
        }

        if (desktopContainmentIds.isEmpty())
            return {};

        // Pass 2: find wallpaper image in desktop containments
        file.seek(0);
        QTextStream in2(&file);
        currentGroup.clear();

        while (!in2.atEnd()) {
            QString line = in2.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;

            if (line.startsWith(QLatin1Char('['))) {
                currentGroup = line;
                continue;
            }

            if (line.startsWith(QLatin1String("Image="))) {
                // Check if group is [Containments][NNN][Wallpaper][org.kde.image][General]
                for (const QString& id : std::as_const(desktopContainmentIds)) {
                    QString expected = QStringLiteral("[Containments][%1][Wallpaper][org.kde.image][General]").arg(id);
                    if (currentGroup == expected) {
                        QString imagePath = line.mid(6); // after "Image="
                        if (imagePath.startsWith(QLatin1String("file://"))) {
                            imagePath = QUrl(imagePath).toLocalFile();
                        }
                        if (!imagePath.isEmpty() && QFile::exists(imagePath)) {
                            qCDebug(lcCore) << "Plasma wallpaper:" << imagePath;
                            return imagePath;
                        }
                    }
                }
            }
        }
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
        // Try swww first (dynamic wallpaper tool, works on Hyprland and sway)
        QString swwwResult = querySwww();
        if (!swwwResult.isEmpty()) {
            qCDebug(lcCore) << "swww wallpaper:" << swwwResult;
            return swwwResult;
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
        QString result = querySwww();
        if (!result.isEmpty()) {
            qCDebug(lcCore) << "swww wallpaper (sway):" << result;
            return result;
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

        // Check color scheme to determine light vs dark wallpaper
        // GNOME uses picture-uri-dark when prefer-dark is active
        QStringList uriKeys = {QStringLiteral("picture-uri")};
        {
            QProcess schemeProc;
            schemeProc.start(
                gsettings,
                {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"), QStringLiteral("color-scheme")});
            if (schemeProc.waitForFinished(1000) && schemeProc.exitCode() == 0) {
                QString scheme = QString::fromUtf8(schemeProc.readAllStandardOutput()).trimmed();
                if (scheme.contains(QLatin1String("dark"))) {
                    uriKeys.prepend(QStringLiteral("picture-uri-dark"));
                }
            }
        }

        for (const QString& key : std::as_const(uriKeys)) {
            QProcess proc;
            proc.start(gsettings, {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.background"), key});
            if (!proc.waitForFinished(1000) || proc.exitCode() != 0)
                continue;

            QString uri = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            uri.remove(QLatin1Char('\''));
            if (uri.startsWith(QLatin1String("file://"))) {
                uri = QUrl(uri).toLocalFile();
            }
            if (QFile::exists(uri)) {
                qCDebug(lcCore) << "GNOME wallpaper:" << uri;
                return uri;
            }
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
