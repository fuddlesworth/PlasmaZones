// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "supportreport.h"
#include "logging.h"
#include "screenmanager.h"
#include "layoutmanager.h"
#include "layout.h"
#include "zone.h"
#include "version.h"
#include "../config/configdefaults.h"
#include "../autotile/AutotileEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QSysInfo>

namespace PlasmaZones {

static constexpr int DefaultSinceMinutes = 30;
static constexpr int MaxLogLines = 2000;
static constexpr int MaxSinceMinutes = 120;
static constexpr qint64 MaxFileSize = 1024 * 1024; // 1 MB

QString SupportReport::redactHomePath(const QString& input)
{
    const QString home = QDir::homePath();
    if (home.isEmpty())
        return input;

    // Negative lookahead prevents partial matches (e.g., /home/n should not
    // match inside /home/nate). Only replace when NOT followed by a word
    // character, dot, or hyphen — characters valid in path components.
    const QRegularExpression re(QRegularExpression::escape(home) + QStringLiteral("(?![\\w.-])"));
    QString result = input;
    result.replace(re, QStringLiteral("~"));
    return result;
}

QString SupportReport::sectionVersion()
{
    return QStringLiteral("**PlasmaZones:** %1\n").arg(VERSION_STRING);
}

QString SupportReport::sectionEnvironment()
{
    QString out;
    out += QStringLiteral("**Qt:** %1\n").arg(QLatin1String(qVersion()));
    out += QStringLiteral("**OS:** %1 %2\n").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
    out += QStringLiteral("**Kernel:** %1\n").arg(QSysInfo::kernelVersion());

    // Compositor info from environment
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    const QString sessionType = qEnvironmentVariable("XDG_SESSION_TYPE");
    const QString waylandDisplay = qEnvironmentVariable("WAYLAND_DISPLAY");
    out += QStringLiteral("**Desktop:** %1 (%2)\n")
               .arg(desktop.isEmpty() ? QStringLiteral("unknown") : desktop,
                    sessionType.isEmpty() ? QStringLiteral("unknown") : sessionType);
    if (!waylandDisplay.isEmpty())
        out += QStringLiteral("**Wayland Display:** %1\n").arg(waylandDisplay);

    // KDE Frameworks version if available
    const QString kfVersion = qEnvironmentVariable("KDE_FRAMEWORKS_VERSION");
    if (!kfVersion.isEmpty())
        out += QStringLiteral("**KDE Frameworks:** %1\n").arg(kfVersion);

    return out;
}

QString SupportReport::sectionScreens(ScreenManager* screenManager)
{
    if (!screenManager)
        return QStringLiteral("*(daemon not running — screen info unavailable)*\n");

    const QVector<QScreen*> screens = screenManager->screens();
    if (screens.isEmpty())
        return QStringLiteral("*(no screens detected)*\n");

    QString out;
    out += QStringLiteral("**Count:** %1\n\n").arg(screens.size());

    for (QScreen* screen : screens) {
        const QRect geo = screen->geometry();
        const QRect avail = ScreenManager::actualAvailableGeometry(screen);
        out += QStringLiteral("- **%1**: %2x%3 @ %4x scale %5")
                   .arg(screen->name())
                   .arg(geo.width())
                   .arg(geo.height())
                   .arg(screen->refreshRate(), 0, 'f', 1)
                   .arg(screen->devicePixelRatio(), 0, 'f', 2);
        if (avail != geo) {
            out += QStringLiteral(" (avail: %1x%2+%3+%4)")
                       .arg(avail.width())
                       .arg(avail.height())
                       .arg(avail.x())
                       .arg(avail.y());
        }
        out += QLatin1String("\n");
    }

    return out;
}

QString SupportReport::readAndRedactFile(const QString& path, const QString& label, const QString& lang)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("*(%1 — %2: %3)*\n").arg(label, redactHomePath(path), file.errorString());

    if (file.size() > MaxFileSize)
        return QStringLiteral("*(%1 too large: %2 bytes)*\n").arg(label).arg(file.size());

    const QString content = QString::fromUtf8(file.readAll());
    return QStringLiteral("```%1\n%2\n```\n").arg(lang, redactHomePath(content));
}

QString SupportReport::sectionConfig()
{
    return readAndRedactFile(ConfigDefaults::configFilePath(), QStringLiteral("config file"));
}

QString SupportReport::sectionLayouts(LayoutManager* layoutManager)
{
    if (!layoutManager)
        return QStringLiteral("*(daemon not running — layout info unavailable)*\n");

    const QList<Layout*> layouts = layoutManager->layouts();
    if (layouts.isEmpty())
        return QStringLiteral("*(no layouts)*\n");

    QString out;
    const Layout* active = layoutManager->activeLayout();

    for (Layout* layout : layouts) {
        const bool isActive = (layout == active);
        out += QStringLiteral("- **%1** (id: %2, zones: %3)%4\n")
                   .arg(layout->name(), layout->id().toString())
                   .arg(layout->zoneCount())
                   .arg(isActive ? QStringLiteral(" **[active]**") : QString());
    }

    return out;
}

QString SupportReport::sectionAutotile(AutotileEngine* autotileEngine)
{
    if (!autotileEngine)
        return QStringLiteral("*(autotile engine not available)*\n");

    QString out;
    out += QStringLiteral("**Enabled:** %1\n")
               .arg(autotileEngine->isEnabled() ? QStringLiteral("yes") : QStringLiteral("no"));

    const auto screens = autotileEngine->autotileScreens();
    if (!screens.isEmpty()) {
        out += QStringLiteral("**Active screens:** %1\n")
                   .arg(QStringList(screens.begin(), screens.end()).join(QStringLiteral(", ")));
    }

    return out;
}

QString SupportReport::sectionSession()
{
    return readAndRedactFile(ConfigDefaults::sessionFilePath(), QStringLiteral("session file"));
}

QString SupportReport::sectionLogs(int sinceMinutes)
{
    QProcess proc;
    proc.setProgram(QStringLiteral("journalctl"));
    proc.setArguments({QStringLiteral("--user"), QStringLiteral("-t"), QStringLiteral("plasmazonesd"),
                       QStringLiteral("--since"), QStringLiteral("%1 min ago").arg(sinceMinutes),
                       QStringLiteral("--no-pager"), QStringLiteral("-o"), QStringLiteral("short-iso")});
    proc.start();

    if (!proc.waitForStarted(3000) || !proc.waitForFinished(30000)) {
        return QStringLiteral("*(journalctl timed out or not available)*\n");
    }

    QByteArray rawOutput = proc.readAllStandardOutput();

    if (proc.exitCode() != 0 && rawOutput.isEmpty()) {
        // Try syslog identifier fallback (some systems use the binary name differently).
        // Use a fresh QProcess to avoid stale state from the first run.
        QProcess fallback;
        fallback.setProgram(QStringLiteral("journalctl"));
        fallback.setArguments({QStringLiteral("--user"), QStringLiteral("--identifier=plasmazonesd"),
                               QStringLiteral("--since"), QStringLiteral("%1 min ago").arg(sinceMinutes),
                               QStringLiteral("--no-pager"), QStringLiteral("-o"), QStringLiteral("short-iso")});
        fallback.start();
        if (!fallback.waitForStarted(3000) || !fallback.waitForFinished(30000))
            return QStringLiteral("*(journalctl not available)*\n");
        rawOutput = fallback.readAllStandardOutput();
    }

    QString output = QString::fromUtf8(rawOutput);
    if (output.trimmed().isEmpty())
        return QStringLiteral("*(no log entries in the last %1 minutes)*\n").arg(sinceMinutes);

    // Cap line count
    const QStringList lines = output.split(QLatin1Char('\n'));
    if (lines.size() > MaxLogLines) {
        output = QStringLiteral("... (%1 lines total, showing last %2) ...\n").arg(lines.size()).arg(MaxLogLines);
        output += lines.mid(lines.size() - MaxLogLines).join(QLatin1Char('\n'));
    }

    return QStringLiteral("```\n%1\n```\n").arg(redactHomePath(output));
}

QString SupportReport::generate(ScreenManager* screenManager, LayoutManager* layoutManager,
                                AutotileEngine* autotileEngine, int sinceMinutes)
{
    sinceMinutes = (sinceMinutes <= 0) ? DefaultSinceMinutes : qMin(sinceMinutes, MaxSinceMinutes);

    QString report;
    report += QStringLiteral("<details>\n<summary>PlasmaZones Support Report</summary>\n\n");

    report += QStringLiteral("## Version\n");
    report += sectionVersion();
    report += QLatin1Char('\n');

    report += QStringLiteral("## Environment\n");
    report += sectionEnvironment();
    report += QLatin1Char('\n');

    report += QStringLiteral("## Screens\n");
    report += sectionScreens(screenManager);
    report += QLatin1Char('\n');

    report += QStringLiteral("## Config\n");
    report += sectionConfig();
    report += QLatin1Char('\n');

    report += QStringLiteral("## Layouts\n");
    report += sectionLayouts(layoutManager);
    report += QLatin1Char('\n');

    report += QStringLiteral("## Autotile\n");
    report += sectionAutotile(autotileEngine);
    report += QLatin1Char('\n');

    report += QStringLiteral("## Session State\n");
    report += sectionSession();
    report += QLatin1Char('\n');

    report += QStringLiteral("## Recent Logs (last %1 minutes)\n").arg(sinceMinutes);
    report += sectionLogs(sinceMinutes);
    report += QLatin1Char('\n');

    report += QStringLiteral("</details>\n");

    return report;
}

} // namespace PlasmaZones
