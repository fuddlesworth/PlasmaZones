// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShellLauncher/AppsProvider.h>

#include "launchhelpers_p.h"

#include <PhosphorShellLauncher/FuzzyMatcher.h>

#include <QCoreApplication>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLocale>
#include <QLoggingCategory>
#include <QProcess>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {
Q_LOGGING_CATEGORY(lcApps, "phosphor.launcher.apps")

// Coalescing window for watcher-driven rescans. One package install emits
// several directoryChanged events, and each rescan re-parses every file in
// every applications directory.
constexpr int kRescanDebounceMs = 250;

QStringList currentDesktopFromEnvironment()
{
    const QString raw = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    return raw.split(u':', Qt::SkipEmptyParts);
}
} // namespace

namespace PhosphorShellLauncher {

using PhosphorRegistry::LauncherResult;

namespace {
// The locale name to scan with, honouring $LANGUAGE.
//
// $LANGUAGE is gettext's ordered preference list ("de:fr" means German, then
// French), and a user who reads more than one language sets it to say which
// they want first. Only its FIRST entry is used here, because the parser
// resolves one locale name into the spec's own fallback chain and cannot
// express a second, independent chain after it. That is still the difference
// between showing such a user their first choice and their system default.
QString preferredLocaleName()
{
    const QString language = qEnvironmentVariable("LANGUAGE");
    const QStringList preferred = language.split(u':', Qt::SkipEmptyParts);
    return preferred.isEmpty() ? QLocale::system().name() : preferred.first();
}
} // namespace

AppsProvider::AppsProvider(QObject* parent)
    : AppsProvider(DesktopEntryScanner::defaultDirectories(), preferredLocaleName(), currentDesktopFromEnvironment(),
                   true, parent)
{
}

AppsProvider::AppsProvider(QStringList directories, QString locale, QStringList currentDesktop, QObject* parent)
    : AppsProvider(std::move(directories), std::move(locale), std::move(currentDesktop), false, parent)
{
}

AppsProvider::AppsProvider(QStringList directories, QString locale, QStringList currentDesktop, bool deferFirstScan,
                           QObject* parent)
    : ILauncherProvider(parent)
    , m_directories(std::move(directories))
    , m_locale(std::move(locale))
    , m_currentDesktop(std::move(currentDesktop))
    , m_watcher(new QFileSystemWatcher(this))
{
    armWatches();
    // Coalesce. A package install or an `update-desktop-database` run emits
    // several directoryChanged events in quick succession, and each one would
    // otherwise drive a full recursive scan-and-parse of every applications
    // directory.
    m_rescanTimer = new QTimer(this);
    m_rescanTimer->setSingleShot(true);
    m_rescanTimer->setInterval(kRescanDebounceMs);
    connect(m_rescanTimer, &QTimer::timeout, this, &AppsProvider::rescan);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        m_rescanTimer->start();
    });

    // On the production path the scan is posted rather than run here. It
    // walks every XDG applications directory and parses every file it finds,
    // and the shell constructs this provider before its first frame, so doing
    // it inline put roughly a thousand file opens on the startup critical
    // path for a surface nobody has opened yet. The queued call lands once
    // the event loop starts; recompute() then emits resultsChanged as usual
    // and the model picks it up with no other change.
    if (deferFirstScan) {
        QMetaObject::invokeMethod(this, &AppsProvider::rescan, Qt::QueuedConnection);
    } else {
        rescan();
    }
}

void AppsProvider::armWatches()
{
    // Re-armed on every rescan, not just at construction. A directory that
    // did not exist yet (a fresh account with no ~/.local/share/applications)
    // would otherwise never be watched, and QFileSystemWatcher drops a path
    // permanently when its directory is deleted, which a package manager that
    // replaces a directory rather than writing into it does routinely.
    const QStringList watched = m_watcher->directories();
    const auto watch = [this, &watched](const QString& dir) {
        if (QFileInfo::exists(dir) && !watched.contains(dir)) {
            m_watcher->addPath(dir);
        }
    };
    for (const QString& dir : std::as_const(m_directories)) {
        watch(dir);
        // Subdirectories too, because the SCAN is recursive: the spec's
        // vendor-prefix layout puts entries in `applications/kde4/` and
        // friends, and a distribution that installs there would have had its
        // apps found at startup and then never refreshed, since the only
        // watched directory never changed.
        QDirIterator it(dir, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            watch(it.next());
        }
    }
}

AppsProvider::~AppsProvider() = default;

QString AppsProvider::id() const
{
    return QStringLiteral("apps");
}

QString AppsProvider::displayName() const
{
    return QCoreApplication::translate("PhosphorShellLauncher", "Applications");
}

QString AppsProvider::iconName() const
{
    return QStringLiteral("applications-all");
}

void AppsProvider::setMaximumResults(int count)
{
    m_maximumResults = std::max(1, count);
}

int AppsProvider::maximumResults() const
{
    return m_maximumResults;
}

QList<DesktopEntry> AppsProvider::entries() const
{
    return m_entries;
}

void AppsProvider::rescan()
{
    armWatches();
    m_entries = DesktopEntryScanner::scan(m_directories, m_locale, m_currentDesktop);
    qCDebug(lcApps) << "scanned" << m_entries.size() << "application(s) from" << m_directories;
    recompute();
}

void AppsProvider::setQuery(const QString& query)
{
    m_query = query;
    recompute();
}

void AppsProvider::recompute()
{
    // Keep the previous answer so the emission below can be conditional.
    // Every resultsChanged costs the model a full reset, which destroys every
    // delegate and drops the surface's selected row, and a watcher-driven
    // rescan that finds nothing new would otherwise do exactly that.
    const QList<LauncherResult> previous = std::move(m_results);
    m_results.clear();
    if (!m_query.isEmpty()) {
        struct Scored
        {
            const DesktopEntry* entry;
            int score;
        };
        // Smart case: a lower-case query matches anything, and a typed
        // capital means the user wants it.
        const bool smartCase = FuzzyMatcher::patternIsCaseSensitive(m_query);
        QList<Scored> scored;
        for (const DesktopEntry& entry : std::as_const(m_entries)) {
            int best = -1;
            if (const auto m = FuzzyMatcher::match(m_query, entry.name, smartCase)) {
                best = m->score;
            }
            // Secondary fields: worth matching, but a name hit must win a
            // tie, so they pay a small fixed penalty.
            const auto consider = [&](const QString& field) {
                if (field.isEmpty()) {
                    return;
                }
                if (const auto m = FuzzyMatcher::match(m_query, field, smartCase)) {
                    best = std::max(best, m->score - SecondaryFieldPenalty);
                }
            };
            consider(entry.genericName);
            for (const QString& keyword : entry.keywords) {
                consider(keyword);
            }
            if (best >= 0) {
                scored.append({&entry, best});
            }
        }
        // Score descending, then name so equal scores are stable and
        // alphabetical rather than in filesystem order.
        std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
            if (a.score != b.score) {
                return a.score > b.score;
            }
            return a.entry->name.localeAwareCompare(b.entry->name) < 0;
        });
        if (scored.size() > m_maximumResults) {
            scored.resize(m_maximumResults);
        }
        m_results.reserve(scored.size());
        for (const Scored& s : std::as_const(scored)) {
            LauncherResult r;
            r.id = s.entry->id;
            r.title = s.entry->name;
            r.subtitle = s.entry->genericName.isEmpty() ? s.entry->comment : s.entry->genericName;
            r.iconName = s.entry->icon;
            r.score = s.score;
            r.primaryActionLabel = QCoreApplication::translate("PhosphorShellLauncher", "Open");
            m_results.append(std::move(r));
        }
    }
    if (m_results == previous) {
        return;
    }
    Q_EMIT resultsChanged();
}

QList<LauncherResult> AppsProvider::results() const
{
    return m_results;
}

bool AppsProvider::activate(const QString& resultId, Activation activation)
{
    if (activation != Activation::Primary) {
        return false;
    }
    for (const DesktopEntry& entry : std::as_const(m_entries)) {
        if (entry.id == resultId) {
            return launch(entry);
        }
    }
    qCWarning(lcApps) << "activate: unknown result" << resultId;
    return false;
}

bool AppsProvider::launch(const DesktopEntry& entry)
{
    QStringList args = entry.execArgs();
    if (args.isEmpty()) {
        qCWarning(lcApps) << "launch:" << entry.id << "has no runnable Exec";
        return false;
    }

    if (entry.terminal && !Private::wrapInTerminal(args)) {
        qCWarning(lcApps) << "launch:" << entry.id
                          << "wants a terminal but neither xdg-terminal-exec nor $TERMINAL is available";
        return false;
    }

    const QString program = args.takeFirst();
    const bool started = QProcess::startDetached(program, args, entry.path);
    if (!started) {
        qCWarning(lcApps) << "launch: failed to start" << program << args;
    }
    return started;
}

} // namespace PhosphorShellLauncher
