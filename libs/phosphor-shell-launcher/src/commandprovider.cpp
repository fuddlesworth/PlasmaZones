// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShellLauncher/CommandProvider.h>

#include "launchhelpers_p.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QStandardPaths>

namespace {
Q_LOGGING_CATEGORY(lcCommand, "phosphor.launcher.command")
}

namespace PhosphorShellLauncher {

using PhosphorRegistry::LauncherResult;

CommandProvider::CommandProvider(QObject* parent)
    : ILauncherProvider(parent)
{
}

CommandProvider::~CommandProvider() = default;

QString CommandProvider::id() const
{
    return QStringLiteral("command");
}

QString CommandProvider::displayName() const
{
    return QCoreApplication::translate("PhosphorShellLauncher", "Run Command");
}

QString CommandProvider::iconName() const
{
    return QStringLiteral("utilities-terminal");
}

QString CommandProvider::resolveProgram(const QString& commandLine)
{
    const QString trimmed = commandLine.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    // The first whitespace-delimited word is the program. Quoting is the
    // shell's problem at run time; for the gate, a quoted program name is
    // rare enough that treating the quote as part of the word (and thus
    // not finding it) is an acceptable miss.
    const qsizetype space = trimmed.indexOf(u' ');
    const QString program = space < 0 ? trimmed : trimmed.left(space);
    if (QDir::isAbsolutePath(program)) {
        return QFileInfo(program).isExecutable() ? program : QString();
    }
    return QStandardPaths::findExecutable(program);
}

void CommandProvider::setQuery(const QString& query)
{
    m_query = query.trimmed();
    m_results.clear();
    if (!resolveProgram(m_query).isEmpty()) {
        LauncherResult r;
        r.id = QStringLiteral("run");
        r.title = m_query;
        r.subtitle = QCoreApplication::translate("PhosphorShellLauncher", "Run as a shell command");
        r.iconName = iconName();
        // The floor: anything that fuzzy-matches the same text ranks above
        // the raw command.
        r.score = 1;
        r.primaryActionLabel = QCoreApplication::translate("PhosphorShellLauncher", "Run");
        r.alternateActionLabel = QCoreApplication::translate("PhosphorShellLauncher", "Run in terminal");
        m_results.append(std::move(r));
    }
    Q_EMIT resultsChanged();
}

QList<LauncherResult> CommandProvider::results() const
{
    return m_results;
}

bool CommandProvider::activate(const QString& resultId, Activation activation)
{
    if (resultId != QLatin1String("run") || m_query.isEmpty()) {
        return false;
    }
    // Through the shell, so pipes, globs and quoting mean what the user
    // expects from typing the same line into a terminal.
    QStringList argv{QStringLiteral("/bin/sh"), QStringLiteral("-c"), m_query};
    if (activation == Activation::Alternate && !Private::wrapInTerminal(argv)) {
        qCWarning(lcCommand) << "no terminal available (neither xdg-terminal-exec nor $TERMINAL)";
        return false;
    }
    const QString program = argv.takeFirst();
    const bool started = QProcess::startDetached(program, argv, QDir::homePath());
    if (!started) {
        qCWarning(lcCommand) << "failed to start" << program << argv;
    }
    return started;
}

} // namespace PhosphorShellLauncher
