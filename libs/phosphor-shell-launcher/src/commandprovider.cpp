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
    // Any whitespace, not just a space: a line pasted from a terminal or a
    // document can be tab-separated, and splitting on the space alone read
    // the whole line as the program name, so no Run row was offered at all.
    qsizetype split = -1;
    for (qsizetype i = 0; i < trimmed.size(); ++i) {
        if (trimmed.at(i).isSpace()) {
            split = i;
            break;
        }
    }
    const QString program = split < 0 ? trimmed : trimmed.left(split);
    if (QDir::isAbsolutePath(program)) {
        return QFileInfo(program).isExecutable() ? program : QString();
    }
    return QStandardPaths::findExecutable(program);
}

void CommandProvider::setQuery(const QString& query)
{
    m_query = query.trimmed();
    // Kept so the emission below can be conditional. Every resultsChanged
    // costs the model a full reset, which destroys every delegate and drops
    // the selected row, and most keystrokes here change nothing: the row is
    // present or absent, and its content is the query itself.
    const QList<LauncherResult> previous = std::move(m_results);
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
    if (m_results == previous) {
        return;
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
    // Re-validated, not assumed. setQuery refuses to offer the row unless the
    // program resolves, but the row the surface holds can outlive that: the
    // binary is uninstalled, or $PATH changes, between typing and Enter.
    // Without this the shell would be handed a line that cannot run and the
    // failure would surface as a silent no-op.
    if (resolveProgram(m_query).isEmpty()) {
        qCWarning(lcCommand) << "refusing to run" << m_query << "— the program no longer resolves";
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
    // The HOME directory, deliberately, where an application launch uses the
    // entry's own Path. A typed command has no declared working directory,
    // and inheriting the shell process's would tie the result to wherever
    // the session happened to start; home is what a terminal would have
    // given the same line.
    const bool started = QProcess::startDetached(program, argv, QDir::homePath());
    if (!started) {
        qCWarning(lcCommand) << "failed to start" << program << argv;
    }
    return started;
}

} // namespace PhosphorShellLauncher
