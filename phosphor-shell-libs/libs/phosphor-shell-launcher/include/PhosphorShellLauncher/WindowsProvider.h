// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/ILauncherProvider.h>
#include <PhosphorShellLauncher/phosphorshelllauncher_export.h>

#include <QList>
#include <QPointer>
#include <QSet>
#include <QString>

QT_BEGIN_NAMESPACE
class QAbstractItemModel;
QT_END_NAMESPACE

namespace PhosphorShellLauncher {

// Open windows, for switching. Lists every toplevel on an empty query
// and fuzzy-matches title and app id otherwise. Enter activates the
// window.
//
// Reads the shell's toplevel model duck-typed: any QAbstractItemModel
// with a `toplevel` role whose objects expose `title` and `appId`
// properties and an invokable `activate()`. That is the shape of
// Phosphor.Shell's Toplevels model, and it is also exactly how QML would
// consume the same objects, so this library stays free of the Wayland
// stack for the sake of one provider. A host on a compositor that does
// not advertise foreign-toplevel simply gets an empty model here, and
// the provider lists nothing.
//
// Rows are addressed by the toplevel object itself (its address), not
// its index: windows open and close while the launcher is up, and an
// index captured at query time would activate the wrong one.
class PHOSPHORSHELLLAUNCHER_EXPORT WindowsProvider : public PhosphorRegistry::ILauncherProvider
{
    Q_OBJECT

public:
    // Null makes the provider inert (no rows), never a crash.
    /// The model is resolved ONCE, here, and so is its "toplevel" role. A
    /// host that publishes the model lazily and constructs this provider
    /// first gets a permanently inert provider and one warning at startup:
    /// there is no signal on a duck-typed dependency to re-read it from, so
    /// the contract is that the model exists by the time this is built.
    explicit WindowsProvider(QAbstractItemModel* toplevels, QObject* parent = nullptr);
    ~WindowsProvider() override;

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QString iconName() const override;
    [[nodiscard]] bool listsOnEmptyQuery() const override;

    void setQuery(const QString& query) override;
    [[nodiscard]] QList<PhosphorRegistry::LauncherResult> results() const override;
    [[nodiscard]] bool activate(const QString& resultId, Activation activation) override;

private Q_SLOTS:
    // A slot, not a plain method: watchToplevels connects to each toplevel's
    // titleChanged / appIdChanged by NAME, since this library does not link
    // the Wayland toplevel type, and a name-based connect needs a name on
    // this side too.
    void recompute();

private:
    /// Subscribe to every toplevel's own title / app-id notifications.
    ///
    /// The list model announces rows arriving and leaving, not a window
    /// RENAMING itself, and a browser or editor retitles constantly. Without
    /// this the launcher listed the title a window had when it opened for
    /// the rest of the session.
    void watchToplevels();
    [[nodiscard]] QObject* toplevelAt(int row) const;
    [[nodiscard]] QObject* toplevelFor(const QString& resultId) const;

    QPointer<QAbstractItemModel> m_toplevels;
    int m_toplevelRole = -1;
    QString m_query;
    QList<PhosphorRegistry::LauncherResult> m_results;
    // Toplevels already subscribed to, so a rescan does not stack a second
    // connection on each one. Entries are dropped as the objects die.
    QSet<QObject*> m_watched;
};

} // namespace PhosphorShellLauncher
