// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/ILauncherProvider.h>
#include <PhosphorShellLauncher/DesktopEntry.h>
#include <PhosphorShellLauncher/phosphorshelllauncher_export.h>

#include <QList>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QFileSystemWatcher;
QT_END_NAMESPACE

namespace PhosphorShellLauncher {

// Installed applications, from the .desktop files on the XDG applications
// path, fuzzy-matched on name, generic name and keywords.
//
// The scan runs once at construction and again whenever one of the
// watched directories changes, so an install or removal shows up on the
// next keystroke without the shell restarting. A rescan is a full walk;
// applications directories are small enough that this is not worth
// tracking per file.
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
    explicit AppsProvider(QObject* parent = nullptr);
    // Injectable wiring for tests and unusual hosts. `directories` in
    // precedence order (first wins per id).
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
    static constexpr int SecondaryFieldPenalty = 4;

    // Everything currently known, unfiltered. For tests and for a host
    // that wants to show a full app grid.
    [[nodiscard]] const QList<DesktopEntry>& entries() const;

    // Re-walk the directories now. Also triggered by the directory watcher.
    void rescan();

    // Start `entry` the way the launcher would (detached, honouring
    // Path and Terminal). Public and static so the surface, or a test,
    // can launch without going through a query. Returns false when the
    // entry has no runnable Exec, or Terminal=true and no terminal could
    // be found.
    [[nodiscard]] static bool launch(const DesktopEntry& entry);

private:
    void recompute();

    QStringList m_directories;
    QString m_locale;
    QStringList m_currentDesktop;
    QList<DesktopEntry> m_entries;
    QFileSystemWatcher* m_watcher = nullptr;
    QString m_query;
    QList<PhosphorRegistry::LauncherResult> m_results;
    int m_maximumResults = 24;
};

} // namespace PhosphorShellLauncher
