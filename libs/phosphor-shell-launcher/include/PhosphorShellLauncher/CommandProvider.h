// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/ILauncherProvider.h>
#include <PhosphorShellLauncher/phosphorshelllauncher_export.h>

#include <QList>
#include <QString>

namespace PhosphorShellLauncher {

// Runs what was typed as a shell command line. Enter runs it detached;
// Alt+Enter runs it inside a terminal.
//
// Offered only when the first word resolves to an executable (on PATH,
// or an absolute path), so ordinary searches like "fire" do not grow a
// "run fire" row. Scores at the floor so an application that matches
// the same text always outranks the raw command.
class PHOSPHORSHELLLAUNCHER_EXPORT CommandProvider : public PhosphorRegistry::ILauncherProvider
{
    Q_OBJECT

public:
    explicit CommandProvider(QObject* parent = nullptr);
    ~CommandProvider() override;

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QString iconName() const override;

    void setQuery(const QString& query) override;
    [[nodiscard]] QList<PhosphorRegistry::LauncherResult> results() const override;
    [[nodiscard]] bool activate(const QString& resultId, Activation activation) override;

    // The executable the command line would start, resolved the way the
    // provider resolves it, or empty when it would not start anything.
    // Public and static so the gating rule is testable on its own.
    [[nodiscard]] static QString resolveProgram(const QString& commandLine);

private:
    QString m_query;
    QList<PhosphorRegistry::LauncherResult> m_results;
};

} // namespace PhosphorShellLauncher
