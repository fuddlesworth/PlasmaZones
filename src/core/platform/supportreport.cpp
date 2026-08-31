// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "supportreport.h"
#include "logging.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "version.h"
#include "config/configdefaults.h"
#include "core/resolve/screenmoderouter.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorZones/AssignmentEntry.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QSysInfo>

#include <algorithm>

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

static QString modeName(PhosphorZones::AssignmentEntry::Mode mode)
{
    switch (mode) {
    case PhosphorZones::AssignmentEntry::Autotile:
        return QStringLiteral("tiling");
    case PhosphorZones::AssignmentEntry::Scrolling:
        return QStringLiteral("scrolling");
    case PhosphorZones::AssignmentEntry::Snapping:
        break;
    }
    return QStringLiteral("snapping");
}

SupportReport::Snapshot SupportReport::collectSnapshot(PhosphorScreens::ScreenManager* screenManager,
                                                       PhosphorZones::LayoutRegistry* layoutManager,
                                                       PhosphorEngine::IPlacementEngine* autotileEngine,
                                                       PhosphorEngine::IPlacementEngine* scrollEngine,
                                                       const ScreenModeRouter* modeRouter)
{
    Snapshot snap;

    if (screenManager) {
        snap.hasScreenManager = true;
        const QVector<PhosphorScreens::PhysicalScreen> screens = screenManager->screens();
        snap.screens.reserve(screens.size());
        for (const PhosphorScreens::PhysicalScreen& screen : screens) {
            Snapshot::ScreenInfo info;
            info.name = screen.name;
            info.id = screen.identifier.isEmpty() ? screen.name : screen.identifier;
            info.geometry = screen.geometry;
            info.available = screenManager->actualAvailableGeometry(screen);
            if (screen.qscreen) {
                info.refreshRate = screen.qscreen->refreshRate();
                info.devicePixelRatio = screen.qscreen->devicePixelRatio();
            }
            snap.screens.append(info);
        }
    }

    if (modeRouter) {
        snap.hasModeRouter = true;
        snap.screenModes.reserve(snap.screens.size());
        for (const Snapshot::ScreenInfo& screen : std::as_const(snap.screens)) {
            snap.screenModes.append({screen.id, modeName(modeRouter->modeFor(screen.id))});
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

    if (scrollEngine) {
        snap.hasScrollEngine = true;
        snap.scrollingEnabled = scrollEngine->isEnabled();
        const auto screens = scrollEngine->activeScreens();
        snap.scrollingScreens = QStringList(screens.begin(), screens.end());
    }

    return snap;
}

QString SupportReport::sectionVersion()
{
    // The generation timestamp anchors the log windows below: the archive
    // filename carries one too, but report.md is often pasted standalone.
    return QStringLiteral("**PlasmaZones:** %1\n**Generated:** %2\n")
        .arg(VERSION_STRING, QDateTime::currentDateTime().toString(Qt::ISODate));
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
    // Persistence is sparse: default-equal values are deleted on save, so
    // every key in the blob is a deviation from defaults. Saying so up front
    // spares triagers diffing the dump against ConfigDefaults.
    return QStringLiteral(
               "*Only settings changed from their defaults are stored. "
               "Any key absent below is at its default value.*\n\n")
        + readAndRedactFile(ConfigDefaults::configFilePath(), QStringLiteral("config file"));
}

QString SupportReport::sectionRules()
{
    // Rules carry per-window and per-screen overrides (gaps, animation timing,
    // opacity, placement) that config.json alone cannot explain — several
    // reports were untriageable without them (discussions #795/#796).
    if (!QFile::exists(ConfigDefaults::rulesFilePath()))
        return QStringLiteral("*(no rules file)*\n");

    // A one-line-per-rule summary ahead of the blob: which rules exist,
    // whether they are enabled, and at what priority — readable without
    // walking the JSON. Best-effort; a parse failure just drops the summary.
    QString summary;
    QFile file(ConfigDefaults::rulesFilePath());
    if (file.open(QIODevice::ReadOnly) && file.size() <= MaxFileSize) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        const QJsonArray rules = doc.object().value(QLatin1String("rules")).toArray();
        if (!rules.isEmpty()) {
            summary += QStringLiteral("**Rules:** %1\n").arg(rules.size());
            for (const QJsonValue& value : rules) {
                const QJsonObject rule = value.toObject();
                QString name = rule.value(QLatin1String("name")).toString();
                if (name.isEmpty())
                    name = QStringLiteral("(unnamed)");
                summary += QStringLiteral("- %1 (%2, priority %3)\n")
                               .arg(name,
                                    rule.value(QLatin1String("enabled")).toBool(true) ? QStringLiteral("enabled")
                                                                                      : QStringLiteral("disabled"))
                               .arg(rule.value(QLatin1String("priority")).toInt());
            }
            summary += QLatin1Char('\n');
        }
    }

    return redactHomePath(summary) + readAndRedactFile(ConfigDefaults::rulesFilePath(), QStringLiteral("rules file"));
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

QString SupportReport::sectionPlacementModes(const Snapshot& snapshot)
{
    QString out;

    // Which engine actually owns each screen is the question most reports
    // hinge on, and the config blob alone cannot answer it (rules and
    // per-context assignments override the global toggles).
    if (snapshot.hasModeRouter && !snapshot.screenModes.isEmpty()) {
        out += QStringLiteral("**Resolved mode per screen (current desktop/activity):**\n");
        for (const auto& mode : snapshot.screenModes)
            out += QStringLiteral("- **%1**: %2\n").arg(mode.screenId, mode.mode);
        out += QLatin1Char('\n');
    } else {
        out += QStringLiteral("*(daemon not running — per-screen mode resolution unavailable)*\n\n");
    }

    if (snapshot.hasAutotileEngine) {
        out += QStringLiteral("**Tiling engine enabled:** %1\n")
                   .arg(snapshot.autotileEnabled ? QStringLiteral("yes") : QStringLiteral("no"));
        if (!snapshot.autotileScreens.isEmpty())
            out += QStringLiteral("**Tiling active screens:** %1\n")
                       .arg(snapshot.autotileScreens.join(QStringLiteral(", ")));
    } else {
        out += QStringLiteral("*(autotile engine not available)*\n");
    }

    if (snapshot.hasScrollEngine) {
        out += QStringLiteral("**Scrolling engine enabled:** %1\n")
                   .arg(snapshot.scrollingEnabled ? QStringLiteral("yes") : QStringLiteral("no"));
        if (!snapshot.scrollingScreens.isEmpty())
            out += QStringLiteral("**Scrolling active screens:** %1\n")
                       .arg(snapshot.scrollingScreens.join(QStringLiteral(", ")));
    } else {
        out += QStringLiteral("*(scrolling engine not available)*\n");
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

// How many of the newest placement entries (by save sequence) to render in
// the Session State summary.
static constexpr int MaxRecentPlacements = 20;

// Renders one placement entry as a single summary line.
static QString placementLine(const QJsonObject& entry)
{
    QString line = QStringLiteral("- `%1`").arg(entry.value(QLatin1String("windowId")).toString());
    line += QStringLiteral(" (seq %1, desktop %2")
                .arg(entry.value(QLatin1String("seq")).toInt())
                .arg(entry.value(QLatin1String("desktop")).toInt());

    const QJsonObject engines = entry.value(QLatin1String("engines")).toObject();
    QStringList engineBits;
    for (auto it = engines.constBegin(); it != engines.constEnd(); ++it) {
        const QJsonObject state = it.value().toObject();
        QString bit = QStringLiteral("%1=%2").arg(it.key(), state.value(QLatin1String("state")).toString());
        if (state.contains(QLatin1String("order")))
            bit += QStringLiteral("/order %1").arg(state.value(QLatin1String("order")).toInt());
        engineBits.append(bit);
    }
    if (!engineBits.isEmpty())
        line += QStringLiteral(", %1").arg(engineBits.join(QStringLiteral(", ")));
    line += QStringLiteral(")\n");
    return line;
}

QString SupportReport::sectionSession()
{
    // The raw session file used to be dumped verbatim and routinely dwarfed
    // the rest of the report (a hundred-plus accumulated placement entries,
    // most for long-closed windows). Summarize instead; the raw session.json
    // ships alongside report.md in the plasmazones-report.sh archive for
    // anyone who needs the full state.
    const QString path = ConfigDefaults::sessionFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("*(session file — %1: %2)*\n").arg(redactHomePath(path), file.errorString());
    if (file.size() > MaxFileSize)
        return QStringLiteral("*(session file exceeds 1 MB limit)*\n");

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        // Unparseable state is itself a diagnostic — fall back to the raw dump.
        return QStringLiteral("*(session file did not parse as JSON: %1 — raw content follows)*\n\n")
                   .arg(parseError.errorString())
            + readAndRedactFile(path, QStringLiteral("session file"));
    }

    const QJsonObject tracking = doc.object().value(QLatin1String("WindowTracking")).toObject();
    const QJsonObject placements = tracking.value(QLatin1String("WindowPlacements")).toObject();

    QString out;
    out += QStringLiteral("**Active layout:** %1\n").arg(tracking.value(QLatin1String("ActiveLayoutId")).toString());
    const QString lastZone = tracking.value(QLatin1String("LastUsedZoneId")).toString();
    out += QStringLiteral("**Last used zone:** %1\n").arg(lastZone.isEmpty() ? QStringLiteral("(none)") : lastZone);
    out += QStringLiteral("**User-snapped classes:** %1\n")
               .arg(tracking.value(QLatin1String("UserSnappedClasses")).toArray().size());

    int total = 0;
    QVector<QJsonObject> entries;
    QStringList classCounts;
    for (auto it = placements.constBegin(); it != placements.constEnd(); ++it) {
        const QJsonArray list = it.value().toArray();
        total += list.size();
        classCounts.append(QStringLiteral("%1 (%2)").arg(it.key()).arg(list.size()));
        for (const QJsonValue& value : list)
            entries.append(value.toObject());
    }
    out += QStringLiteral("**Tracked placements:** %1 entries across %2 window classes\n")
               .arg(total)
               .arg(placements.size());
    if (!classCounts.isEmpty())
        out += QStringLiteral("**Per class:** %1\n").arg(classCounts.join(QStringLiteral(", ")));

    if (!entries.isEmpty()) {
        std::sort(entries.begin(), entries.end(), [](const QJsonObject& a, const QJsonObject& b) {
            return a.value(QLatin1String("seq")).toInt() > b.value(QLatin1String("seq")).toInt();
        });
        const int shown = qMin<int>(entries.size(), MaxRecentPlacements);
        out += QStringLiteral("\n**Most recent placements (newest first, showing %1 of %2):**\n").arg(shown).arg(total);
        for (int i = 0; i < shown; ++i)
            out += placementLine(entries.at(i));
    }

    out += QStringLiteral(
        "\n*Summary only. The full session.json is included in the report archive "
        "when generated with plasmazones-report.sh.*\n");
    return redactHomePath(out);
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

QString SupportReport::sectionEffectLogs(int sinceMinutes, bool bridgeRegistered)
{
    // The KWin effect runs inside the kwin_wayland process, so its journal
    // entries are tagged "kwin_wayland", not "plasmazonesd" — sectionLogs()
    // never captures them. Without this section a non-registering effect is
    // invisible in the report.
    //
    // When the compositor bridge IS registered, an empty result just means the
    // effect logged nothing in the window — saying "likely not loaded" there
    // would contradict the Compositor Bridge section a few lines up.
    const QString quietSuffix = bridgeRegistered
        ? QStringLiteral("the effect is connected and simply logged nothing in this window")
        : QStringLiteral("the KWin effect is likely not loaded");

    const QByteArray rawOutput = collectJournal(QStringLiteral("kwin_wayland"), sinceMinutes);
    if (rawOutput.isEmpty())
        return QStringLiteral("*(no kwin_wayland journal in the last %1 minutes, or journalctl unavailable — %2)*\n")
            .arg(sinceMinutes)
            .arg(quietSuffix);

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
        return QStringLiteral("*(no PlasmaZones effect log entries in the last %1 minutes — %2)*\n")
            .arg(sinceMinutes)
            .arg(quietSuffix);
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

    report += QStringLiteral("## Placement Modes\n");
    report += sectionPlacementModes(snapshot);
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
    report += sectionEffectLogs(sinceMinutes, snapshot.hasBridgeInfo && snapshot.bridgeRegistered);
    report += QLatin1Char('\n');

    // Sanitize any literal </details> in section content that would prematurely
    // close the collapsible block when rendered in GitHub Issues/Discussions.
    report.replace(QStringLiteral("</details>"), QStringLiteral("&lt;/details&gt;"));

    report += QStringLiteral("</details>\n");

    return report;
}

QString SupportReport::generate(PhosphorScreens::ScreenManager* screenManager,
                                PhosphorZones::LayoutRegistry* layoutManager,
                                PhosphorEngine::IPlacementEngine* autotileEngine, int sinceMinutes,
                                PhosphorEngine::IPlacementEngine* scrollEngine, const ScreenModeRouter* modeRouter)
{
    return generateFromSnapshot(collectSnapshot(screenManager, layoutManager, autotileEngine, scrollEngine, modeRouter),
                                sinceMinutes);
}

} // namespace PlasmaZones
