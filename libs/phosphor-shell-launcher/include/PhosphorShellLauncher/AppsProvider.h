// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/ILauncherProvider.h>
#include <PhosphorShellLauncher/DesktopEntry.h>
#include <PhosphorShellLauncher/FuzzyMatcher.h>
#include <PhosphorShellLauncher/phosphorshelllauncher_export.h>

#include <QList>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QFileSystemWatcher;
class QTimer;
QT_END_NAMESPACE

namespace PhosphorShellLauncher {

// Installed applications, from the .desktop files on the XDG applications
// path, fuzzy-matched on name, generic name and keywords.
//
// The scan runs once at startup and again whenever one of the watched
// directories changes, so an install or removal shows up on the next
// keystroke without the shell restarting. Watcher-driven rescans are
// coalesced, because one package install emits several change events. A
// rescan is a full walk; applications directories are small enough that this
// is not worth tracking per file.
//
// Whether the FIRST scan is synchronous depends on which constructor is
// used; see each one.
//
// Ranking: each entry's score is the best fuzzy score across its name,
// generic name and each keyword, with a fixed penalty on anything that was
// NOT the name so "Files" the app outranks an app that merely lists
// "files" as a keyword.
class PHOSPHORSHELLLAUNCHER_EXPORT AppsProvider : public PhosphorRegistry::ILauncherProvider
{
    Q_OBJECT

public:
    // Production wiring: the XDG applications directories, the system
    // locale, and XDG_CURRENT_DESKTOP.
    //
    // The first scan is DEFERRED to the event loop on this path. A shell
    // constructs its providers before the first frame, and walking every
    // applications directory there costs roughly a thousand file opens for a
    // surface the user may never open.
    explicit AppsProvider(QObject* parent = nullptr);
    // Injectable wiring for tests and unusual hosts. `directories` in
    // precedence order (first wins per id).
    //
    // Scans SYNCHRONOUSLY, so results are available as soon as the object
    // is: a caller passing its own directories is not on a shell's startup
    // path and generally wants to query immediately.
    AppsProvider(QStringList directories, QString locale, QStringList currentDesktop, QObject* parent = nullptr);
    ~AppsProvider() override;

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QString iconName() const override;

    void setQuery(const QString& query) override;
    [[nodiscard]] QList<PhosphorRegistry::LauncherResult> results() const override;
    [[nodiscard]] bool activate(const QString& resultId, Activation activation) override;

    // How many rows a query may yield; the rest are dropped after ranking.
    // Public so a host can retune it, and so tests can pin the cap.
    void setMaximumResults(int count);
    [[nodiscard]] int maximumResults() const;

    // Score penalty applied to a match against anything but the name.
    // Expressed against the matcher's own scale rather than as a bare
    // number. Every other constant in the ranking derives from ScoreMatch,
    // so retuning that would silently rescale them all and leave this one
    // meaning something different. A quarter of one character match is
    // enough to break a tie between a name hit and a keyword hit without
    // outweighing a genuinely better match.
    static constexpr int SecondaryFieldPenalty = FuzzyMatcher::ScoreMatch / 4;

    // Everything currently known, unfiltered. For tests and for a host
    // that wants to show a full app grid.
    //
    // Returned BY VALUE on purpose. The watcher replaces this list wholesale
    // from the event loop when an application is installed or removed, so a
    // reference handed out here would dangle across any turn of the event
    // loop, which is exactly what a host showing a grid would do with it.
    [[nodiscard]] QList<DesktopEntry> entries() const;

    // Re-walk the directories now. Also triggered by the directory watcher,
    // through a short debounce.
    //
    // The CONSTRUCTOR does not call this inline: it walks every XDG
    // applications directory and parses every file it finds, which is too
    // much to put on a shell's pre-first-frame path for a surface the user
    // may never open. The first scan is posted to the event loop instead, so
    // results arrive shortly after startup rather than delaying it.
    void rescan();

    // Start `entry` the way the launcher would (detached, honouring
    // Path and Terminal). Public and static so the surface, or a test,
    // can launch without going through a query. Returns false when the
    // entry has no runnable Exec, or Terminal=true and no terminal could
    // be found.
    /// Launch one entry directly, bypassing the id lookup activate() does.
    ///
    /// PUBLIC for a host that has a DesktopEntry in hand from somewhere
    /// other than this provider's own results: a recent-apps list, a file
    /// manager's open-with. Nothing in this repo calls it, so it is API
    /// surface rather than used code, and it is the one place a caller can
    /// launch something this provider never offered.
    [[nodiscard]] static bool launch(const DesktopEntry& entry);

private:
    // Shared body for both public constructors. `deferFirstScan` decides
    // whether the initial walk runs inline or is posted to the event loop.
    AppsProvider(QStringList directories, QString locale, QStringList currentDesktop, bool deferFirstScan,
                 QObject* parent);

    void recompute();
    // Add a watch for every configured directory that exists and is not
    // already watched. Re-run on each rescan, because a directory can be
    // created later or replaced out from under an existing watch.
    void armWatches();

    QStringList m_directories;
    QString m_locale;
    QStringList m_currentDesktop;
    QList<DesktopEntry> m_entries;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_rescanTimer = nullptr;
    QString m_query;
    QList<PhosphorRegistry::LauncherResult> m_results;
    int m_maximumResults = 24;
};

} // namespace PhosphorShellLauncher
