// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

// Private helpers shared by the providers that start processes. Not
// installed, not exported.

#include <QStandardPaths>
#include <QStringList>

namespace PhosphorShellLauncher::Private {

// Rewrite `argv` so it runs inside a terminal emulator. The spec leaves
// the terminal to the environment: prefer the xdg-terminal-exec
// convention when it is installed, fall back to $TERMINAL with the
// near-universal -e, and otherwise refuse — a terminal application
// started headless exits at once, which reads to the user as "nothing
// happened". Returns false (argv untouched) on refusal.
inline bool wrapInTerminal(QStringList& argv)
{
    const QString xdgTerminal = QStandardPaths::findExecutable(QStringLiteral("xdg-terminal-exec"));
    if (!xdgTerminal.isEmpty()) {
        // The `--` separator, so a command whose first word begins with a
        // dash is passed through as the command rather than parsed as an
        // option to xdg-terminal-exec itself.
        argv.prepend(QStringLiteral("--"));
        argv.prepend(xdgTerminal);
        return true;
    }
    const QString terminal = qEnvironmentVariable("TERMINAL");
    if (terminal.isEmpty()) {
        return false;
    }
    argv.prepend(QStringLiteral("-e"));
    argv.prepend(terminal);
    return true;
}

} // namespace PhosphorShellLauncher::Private
