// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShellLauncher/AppsProvider.h>

#include "launchhelpers_p.h"

#include <PhosphorShellLauncher/FuzzyMatcher.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLocale>
#include <QLoggingCategory>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace {
Q_LOGGING_CATEGORY(lcApps, "phosphor.launcher.apps")

QStringList currentDesktopFromEnvironment()
{
    const QString raw = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    return raw.split(u':', Qt::SkipEmptyParts);
}
} // namespace

namespace PhosphorShellLauncher {

using PhosphorRegistry::LauncherResult;

AppsProvider::AppsProvider(QObject* parent)
    : AppsProvider(DesktopEntryScanner::defaultDirectories(), QLocale::system().name(), currentDesktopFromEnvironment(),
                   parent)
{
}

AppsProvider::AppsProvider(QStringList directories, QString locale, QStringList currentDesktop, QObject* parent)
    : ILauncherProvider(parent)
    , m_directories(std::move(directories))
    , m_locale(std::move(locale))
    , m_currentDesktop(std::move(currentDesktop))
    , m_watcher(new QFileSystemWatcher(this))
{
    // Watch whichever directories exist now. A directory created later
    // (first app installed under ~/.local) is picked up on the next
    // rescan triggered by an existing one; tracking creation of the
    // directories themselves is not worth a second watcher.
    for (const QString& dir : std::as_const(m_directories)) {
        if (QFileInfo::exists(dir)) {
            m_watcher->addPath(dir);
        }
    }
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &AppsProvider::rescan);
    rescan();
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

const QList<DesktopEntry>& AppsProvider::entries() const
{
    return m_entries;
}

void AppsProvider::rescan()
{
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
    m_results.clear();
    if (!m_query.isEmpty()) {
        struct Scored
        {
            const DesktopEntry* entry;
            int score;
        };
        QList<Scored> scored;
        for (const DesktopEntry& entry : std::as_const(m_entries)) {
            int best = -1;
            if (const auto m = FuzzyMatcher::match(m_query, entry.name)) {
                best = m->score;
            }
            // Secondary fields: worth matching, but a name hit must win a
            // tie, so they pay a small fixed penalty.
            const auto consider = [&](const QString& field) {
                if (field.isEmpty()) {
                    return;
                }
                if (const auto m = FuzzyMatcher::match(m_query, field)) {
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
