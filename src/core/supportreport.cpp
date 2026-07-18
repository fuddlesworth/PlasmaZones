// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "supportreport.h"
#include "logging.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "version.h"
#include "../config/configdefaults.h"
#include <PhosphorEngine/IPlacementEngine.h>

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

SupportReport::Snapshot SupportReport::collectSnapshot(PhosphorScreens::ScreenManager* screenManager,
                                                       PhosphorZones::LayoutRegistry* layoutManager,
                                                       PhosphorEngine::IPlacementEngine* autotileEngine)
{
    Snapshot snap;

    if (screenManager) {
        snap.hasScreenManager = true;
        const QVector<PhosphorScreens::PhysicalScreen> screens = screenManager->screens();
        snap.screens.reserve(screens.size());
        for (const PhosphorScreens::PhysicalScreen& screen : screens) {
            Snapshot::ScreenInfo info;
            info.name = screen.name;
            info.geometry = screen.geometry;
            info.available = screenManager->actualAvailableGeometry(screen);
            if (screen.qscreen) {
                info.refreshRate = screen.qscreen->refreshRate();
                info.devicePixelRatio = screen.qscreen->devicePixelRatio();
            }
            snap.screens.append(info);
        }
    }

    if (layoutManager) {
        snap.hasLayoutManager = true;
        const QList<PhosphorZones::Layout*> layouts = layoutManager->layouts();
        const PhosphorZones::Layout* active = layoutManager->activeLayout();
        snap.layouts.reserve(layouts.size());
        for (PhosphorZones::Layout* layout : layouts) {
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
        const auto screens = autotileEngine->activeScreens();
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

QString SupportReport::sectionRules()
{
    // Rules carry per-window and per-screen overrides (gaps, animation timing,
    // opacity, placement) that config.json alone cannot explain — several
    // reports were untriageable without them (discussions #795/#796).
    if (!QFile::exists(ConfigDefaults::rulesFilePath()))
        return QStringLiteral("*(no rules file)*\n");
    return readAndRedactFile(ConfigDefaults::rulesFilePath(), QStringLiteral("rules file"));
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

QString SupportReport::sectionCompositorBridge(const Snapshot& snapshot)
{
    if (!snapshot.hasBridgeInfo)
        return QStringLiteral("*(daemon not running — compositor bridge state unavailable)*\n");

    if (snapshot.bridgeRegistered) {
        QString out;
        out += QStringLiteral("**Status:** connected\n");
        out += QStringLiteral("**Compositor:** %1\n").arg(snapshot.bridgeName);
        out += QStringLiteral("**Effect protocol version:** %1\n").arg(snapshot.bridgeVersion);
        if (!snapshot.bridgeCapabilities.isEmpty()) {
            out += QStringLiteral("**Capabilities:** %1\n").arg(snapshot.bridgeCapabilities.join(QStringLiteral(", ")));
        }
        return out;
    }

    // Not registered: this is the failure mode behind "dragging and shortcuts
    // do nothing" — the daemon runs fine but has no window control without the
    // effect. Spell out the fix so the report is self-diagnosing.
    return QStringLiteral(
        "**Status:** NOT CONNECTED — the KWin effect has not registered with the daemon.\n\n"
        "Window dragging, keyboard shortcuts, and snapping cannot work without it. "
        "Verify that the **PlasmaZones** effect is enabled in System Settings → Desktop Effects, "
        "then restart the Plasma session so KWin loads it. See the KWin Effect Logs section below "
        "for why the effect failed to load or register.\n");
}

QString SupportReport::sectionSession()
{
    return readAndRedactFile(ConfigDefaults::sessionFilePath(), QStringLiteral("session file"));
}

static QStringList journalctlArgs(const QString& identifier, int sinceMinutes, bool longForm = false,
                                  bool userScope = true)
{
    QStringList args;
    if (userScope)
        args << QStringLiteral("--user");
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

// Collects the journal for `identifier` over the last `sinceMinutes`. Tries the
// user journal with -t, then --identifier (some systemd versions report the
// syslog tag differently), then the system journal (a compositor that is not a
// systemd user service logs there instead of the user journal). Returns the raw
// output, or an empty QByteArray if journalctl is unavailable / produced nothing.
static QByteArray collectJournal(const QString& identifier, int sinceMinutes)
{
    QByteArray raw = runJournalctl(journalctlArgs(identifier, sinceMinutes));
    if (QString::fromUtf8(raw).trimmed().isEmpty())
        raw = runJournalctl(journalctlArgs(identifier, sinceMinutes, true));
    if (QString::fromUtf8(raw).trimmed().isEmpty())
        raw = runJournalctl(journalctlArgs(identifier, sinceMinutes, false, /*userScope=*/false));
    return raw;
}

// Caps `lines` to the most recent MaxLogLines, prepending a truncation notice
// when lines were dropped. Keeping the *newest* lines mirrors what a support
// archive needs (the entries around a failure) and stays consistent with
// scripts/plasmazones-report.sh.
static QString capLogLines(const QStringList& lines)
{
    if (lines.size() <= MaxLogLines)
        return lines.join(QLatin1Char('\n'));

    QString output = QStringLiteral("... (%1 lines total, showing last %2) ...\n").arg(lines.size()).arg(MaxLogLines);
    output += lines.mid(lines.size() - MaxLogLines).join(QLatin1Char('\n'));
    return output;
}

QString SupportReport::sectionLogs(int sinceMinutes)
{
    // collectSnapshot()/generateFromSnapshot() run off the main thread via
    // QtConcurrent::run in ControlAdaptor::generateSupportReport.
    const QByteArray rawOutput = collectJournal(QStringLiteral("plasmazonesd"), sinceMinutes);
    if (rawOutput.isEmpty())
        return QStringLiteral("*(no log entries in the last %1 minutes, or journalctl unavailable)*\n")
            .arg(sinceMinutes);

    const QString output = QString::fromUtf8(rawOutput);
    if (output.trimmed().isEmpty())
        return QStringLiteral("*(no log entries in the last %1 minutes)*\n").arg(sinceMinutes);

    return QStringLiteral("```\n%1\n```\n").arg(redactHomePath(capLogLines(output.split(QLatin1Char('\n')))));
}

QString SupportReport::sectionEffectLogs(int sinceMinutes)
{
    // The KWin effect runs inside the kwin_wayland process, so its journal
    // entries are tagged "kwin_wayland", not "plasmazonesd" — sectionLogs()
    // never captures them. Without this section a non-registering effect is
    // invisible in the report.
    const QByteArray rawOutput = collectJournal(QStringLiteral("kwin_wayland"), sinceMinutes);
    if (rawOutput.isEmpty())
        return QStringLiteral(
                   "*(no kwin_wayland journal in the last %1 minutes, or journalctl unavailable — "
                   "the KWin effect is likely not loaded)*\n")
            .arg(sinceMinutes);

    // Keep only PlasmaZones effect lines — the rest of the kwin_wayland journal
    // is unrelated compositor noise. Every effect logging category begins with
    // "plasmazones" (e.g. "plasmazones.effect"), and Qt's default message
    // pattern prints the category, so a substring match catches every line.
    QStringList kept;
    const QStringList lines = QString::fromUtf8(rawOutput).split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (line.contains(QLatin1String("plasmazones"), Qt::CaseInsensitive))
            kept.append(line);
    }

    if (kept.isEmpty()) {
        return QStringLiteral(
                   "*(no PlasmaZones effect log entries in the last %1 minutes — "
                   "the KWin effect is likely not loaded)*\n")
            .arg(sinceMinutes);
    }

    return QStringLiteral("```\n%1\n```\n").arg(redactHomePath(capLogLines(kept)));
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

    report += QStringLiteral("## Rules\n");
    report += sectionRules();
    report += QLatin1Char('\n');

    report += QStringLiteral("## Layouts\n");
    report += sectionLayouts(snapshot);
    report += QLatin1Char('\n');

    report += QStringLiteral("## Autotile\n");
    report += sectionAutotile(snapshot);
    report += QLatin1Char('\n');

    report += QStringLiteral("## Compositor Bridge\n");
    report += sectionCompositorBridge(snapshot);
    report += QLatin1Char('\n');

    report += QStringLiteral("## Session State\n");
    report += sectionSession();
    report += QLatin1Char('\n');

    report += QStringLiteral("## Recent Logs (last %1 minutes)\n").arg(sinceMinutes);
    report += sectionLogs(sinceMinutes);
    report += QLatin1Char('\n');

    report += QStringLiteral("## KWin Effect Logs (last %1 minutes)\n").arg(sinceMinutes);
    report += sectionEffectLogs(sinceMinutes);
    report += QLatin1Char('\n');

    // Sanitize any literal </details> in section content that would prematurely
    // close the collapsible block when rendered in GitHub Issues/Discussions.
    report.replace(QStringLiteral("</details>"), QStringLiteral("&lt;/details&gt;"));

    report += QStringLiteral("</details>\n");

    return report;
}

QString SupportReport::generate(PhosphorScreens::ScreenManager* screenManager,
                                PhosphorZones::LayoutRegistry* layoutManager,
                                PhosphorEngine::IPlacementEngine* autotileEngine, int sinceMinutes)
{
    return generateFromSnapshot(collectSnapshot(screenManager, layoutManager, autotileEngine), sinceMinutes);
}

} // namespace PlasmaZones
