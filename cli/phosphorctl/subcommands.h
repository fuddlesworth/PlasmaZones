// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStringList>

namespace Phosphorctl {

// Subcommand entry points. Each takes the post-subcommand argument
// list (after the subcommand name itself), plus the resolved socket
// path. Returns the process exit code:
//   0 - success
//   1 - usage error (printed via QCommandLineParser)
//   2 - connection error
//   3 - server-side error
[[nodiscard]] int runCall(const QStringList& args, const QString& socketPath);
[[nodiscard]] int runList(const QStringList& args, const QString& socketPath);
[[nodiscard]] int runSchema(const QStringList& args, const QString& socketPath);
[[nodiscard]] int runSubscribe(const QStringList& args, const QString& socketPath);

} // namespace Phosphorctl
