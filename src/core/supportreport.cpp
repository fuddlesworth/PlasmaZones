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

// These constants are mirrored in scripts/plasmazones-report.sh — keep in sync.
static constexpr int DefaultSinceMinutes = 30;
static constexpr int MaxLogLines = 2000;
static constexpr int MaxSinceMinutes = 120;
static constexpr qint64 MaxFileSize = 1024 * 1024; // 1 MB

QString SupportReport::redactHomePath(const QString& input)
{
    const QString home = QDir::homePath();
    if (home.isEmpty() || home == QLatin1String("/"))
        return input;

    // Match home path when followed by a separator (/ or end-of-string),
    // preventing partial matches (e.g., /home/user must not match /home/username).
    // Cache the compiled regex per-thread — redactHomePath is called per-line on
    // potentially 2000+ log lines, and generateFromSnapshot runs off the main thread
    // via QtConcurrent::run, so plain `static` would be a data race.
    thread_local QString cachedHome;
    thread_local QRegularExpression re;
    if (cachedHome != home) {
        cachedHome = home;
        re = QRegularExpression(QRegularExpression::escape(home) + QStringLiteral("(?=[/\\s]|$)"));
    }
    QString result = input;
    result.replace(re, QStringLiteral("~"));
    return result;
}

SupportReport::Snapshot SupportReport::collectSnapshot(ScreenManager* screenManager, LayoutManager* layoutManager,
                                                       AutotileEngine* autotileEngine)
{
    Snapshot snap;

    if (screenManager) {
        snap.hasScreenManager = true;
        const QVector<QScreen*> screens = screenManager->screens();
        snap.screens.reserve(screens.size());
        for (QScreen* screen : screens) {
            Snapshot::ScreenInfo info;
            info.name = screen->name();
            info.geometry = screen->geometry();
            info.available = ScreenManager::actualAvailableGeometry(screen);
            info.refreshRate = screen->refreshRate();
            info.devicePixelRatio = screen->devicePixelRatio();
            snap.screens.append(info);
        }
    }

    if (layoutManager) {
        snap.hasLayoutManager = true;
        const QList<Layout*> layouts = layoutManager->layouts();
        const Layout* active = layoutManager->activeLayout();
        snap.layouts.reserve(layouts.size());
        for (Layout* layout : layouts) {
            Snapshot::LayoutInfo info;
            info.name = layout->name();
            info.id = layout->id().toString();
            info.zoneCount = layout->zoneCount();
            info.isActive = (layout == active);
            snap.layouts.append(info);
        }
    }

    if (autotileEngine) {
        snap.hasAutotileEngine = true;
        snap.autotileEnabled = autotileEngine->isEnabled();
        const auto screens = autotileEngine->autotileScreens();
        snap.autotileScreens = QStringList(screens.begin(), screens.end());
    }

    return snap;
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

    // KDE version info — KDE_SESSION_VERSION is set reliably by startkde/startplasma,
    // KDE_FRAMEWORKS_VERSION is set by some distros but not all
    const QString sessionVersion = qEnvironmentVariable("KDE_SESSION_VERSION");
    if (!sessionVersion.isEmpty())
        out += QStringLiteral("**KDE Session:** %1\n").arg(sessionVersion);
    const QString kfVersion = qEnvironmentVariable("KDE_FRAMEWORKS_VERSION");
    if (!kfVersion.isEmpty())
        out += QStringLiteral("**KDE Frameworks:** %1\n").arg(kfVersion);

    return out;
}

QString SupportReport::sectionScreens(const Snapshot& snapshot)
{
    if (!snapshot.hasScreenManager)
        return QStringLiteral("*(daemon not running — screen info unavailable)*\n");

    if (snapshot.screens.isEmpty())
        return QStringLiteral("*(no screens detected)*\n");

    QString out;
    out += QStringLiteral("**Count:** %1\n\n").arg(snapshot.screens.size());

    for (const auto& screen : snapshot.screens) {
        out += QStringLiteral("- **%1**: %2x%3 @ %4 Hz, scale %5")
                   .arg(screen.name)
                   .arg(screen.geometry.width())
                   .arg(screen.geometry.height())
                   .arg(screen.refreshRate, 0, 'f', 1)
                   .arg(screen.devicePixelRatio, 0, 'f', 2);
        if (screen.available != screen.geometry) {
            out += QStringLiteral(" (avail: %1x%2+%3+%4)")
                       .arg(screen.available.width())
                       .arg(screen.available.height())
                       .arg(screen.available.x())
                       .arg(screen.available.y());
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

    const QByteArray data = file.read(MaxFileSize + 1);
    if (data.size() > MaxFileSize)
        return QStringLiteral("*(%1 exceeds 1 MB limit)*\n").arg(label);

    const QString content = QString::fromUtf8(data);
    return QStringLiteral("```%1\n%2\n```\n").arg(lang, redactHomePath(content));
}

QString SupportReport::sectionConfig()
{
    return readAndRedactFile(ConfigDefaults::configFilePath(), QStringLiteral("config file"));
}

QString SupportReport::sectionLayouts(const Snapshot& snapshot)
{
    if (!snapshot.hasLayoutManager)
        return QStringLiteral("*(daemon not running — layout info unavailable)*\n");

    if (snapshot.layouts.isEmpty())
        return QStringLiteral("*(no layouts)*\n");

    QString out;
    for (const auto& layout : snapshot.layouts) {
        out += QStringLiteral("- **%1** (id: %2, zones: %3)%4\n")
                   .arg(layout.name, layout.id)
                   .arg(layout.zoneCount)
                   .arg(layout.isActive ? QStringLiteral(" **[active]**") : QString());
    }

    return out;
}

QString SupportReport::sectionAutotile(const Snapshot& snapshot)
{
    if (!snapshot.hasAutotileEngine)
        return QStringLiteral("*(autotile engine not available)*\n");

    QString out;
    out += QStringLiteral("**Enabled:** %1\n")
               .arg(snapshot.autotileEnabled ? QStringLiteral("yes") : QStringLiteral("no"));

    if (!snapshot.autotileScreens.isEmpty()) {
        out += QStringLiteral("**Active screens:** %1\n").arg(snapshot.autotileScreens.join(QStringLiteral(", ")));
    }

    return out;
}

QString SupportReport::sectionSession()
{
    return readAndRedactFile(ConfigDefaults::sessionFilePath(), QStringLiteral("session file"));
}

static QStringList journalctlArgs(const QString& identifier, int sinceMinutes, bool longForm = false)
{
    QStringList args{QStringLiteral("--user")};
    if (longForm) {
        args << QStringLiteral("--identifier=%1").arg(identifier);
    } else {
        args << QStringLiteral("-t") << identifier;
    }
    args << QStringLiteral("--since") << QStringLiteral("%1 min ago").arg(sinceMinutes) << QStringLiteral("--no-pager")
         << QStringLiteral("-o") << QStringLiteral("short-iso");
    return args;
}

static QByteArray runJournalctl(const QStringList& args)
{
    QProcess proc;
    proc.setProgram(QStringLiteral("journalctl"));
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(3000))
        return {};
    proc.closeWriteChannel();
    if (!proc.waitForFinished(12000))
        return {};
    return proc.readAllStandardOutput();
}

QString SupportReport::sectionLogs(int sinceMinutes)
{
    // thread_local for consistency with redactHomePath — sectionLogs runs off the
    // main thread via QtConcurrent::run in ControlAdaptor::generateSupportReport.
    thread_local const QString tag = QStringLiteral("plasmazonesd");
    QByteArray rawOutput = runJournalctl(journalctlArgs(tag, sinceMinutes));

    // Fall back to --identifier if -t returned nothing useful.
    // Some systemd versions report the syslog tag differently.
    if (QString::fromUtf8(rawOutput).trimmed().isEmpty()) {
        rawOutput = runJournalctl(journalctlArgs(tag, sinceMinutes, true));
    }

    if (rawOutput.isEmpty())
        return QStringLiteral("*(journalctl timed out or not available)*\n");

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

QString SupportReport::generateFromSnapshot(const Snapshot& snapshot, int sinceMinutes)
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
    report += sectionScreens(snapshot);
    report += QLatin1Char('\n');

    report += QStringLiteral("## Config\n");
    report += sectionConfig();
    report += QLatin1Char('\n');

    report += QStringLiteral("## Layouts\n");
    report += sectionLayouts(snapshot);
    report += QLatin1Char('\n');

    report += QStringLiteral("## Autotile\n");
    report += sectionAutotile(snapshot);
    report += QLatin1Char('\n');

    report += QStringLiteral("## Session State\n");
    report += sectionSession();
    report += QLatin1Char('\n');

    report += QStringLiteral("## Recent Logs (last %1 minutes)\n").arg(sinceMinutes);
    report += sectionLogs(sinceMinutes);
    report += QLatin1Char('\n');

    // Sanitize any literal </details> in section content that would prematurely
    // close the collapsible block when rendered in GitHub Issues/Discussions.
    report.replace(QStringLiteral("</details>"), QStringLiteral("&lt;/details&gt;"));

    report += QStringLiteral("</details>\n");

    return report;
}

QString SupportReport::generate(ScreenManager* screenManager, LayoutManager* layoutManager,
                                AutotileEngine* autotileEngine, int sinceMinutes)
{
    return generateFromSnapshot(collectSnapshot(screenManager, layoutManager, autotileEngine), sinceMinutes);
}

} // namespace PlasmaZones
