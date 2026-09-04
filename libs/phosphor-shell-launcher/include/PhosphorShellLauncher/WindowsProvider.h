// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/ILauncherProvider.h>
#include <PhosphorShellLauncher/phosphorshelllauncher_export.h>

#include <QList>
#include <QPointer>
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
    explicit WindowsProvider(QAbstractItemModel* toplevels, QObject* parent = nullptr);
    ~WindowsProvider() override;

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QString iconName() const override;
    [[nodiscard]] bool listsOnEmptyQuery() const override;

    void setQuery(const QString& query) override;
    [[nodiscard]] QList<PhosphorRegistry::LauncherResult> results() const override;
    [[nodiscard]] bool activate(const QString& resultId, Activation activation) override;

private:
    void recompute();
    [[nodiscard]] QObject* toplevelAt(int row) const;
    [[nodiscard]] QObject* toplevelFor(const QString& resultId) const;

    QPointer<QAbstractItemModel> m_toplevels;
    int m_toplevelRole = -1;
    QString m_query;
    QList<PhosphorRegistry::LauncherResult> m_results;
};

} // namespace PhosphorShellLauncher
