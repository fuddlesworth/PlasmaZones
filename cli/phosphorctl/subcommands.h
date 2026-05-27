// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

namespace Phosphorctl {

// Strip `--socket PATH` / `--socket=PATH` / `-s PATH` from the
// passed-in arg list and return the resolved path (or empty if no
// flag was supplied). Called by both the top-level dispatcher and
// the per-subcommand handlers so `phosphorctl --socket /x call ...`
// AND `phosphorctl call ... --socket /x` both work.
[[nodiscard]] QString stripSocketFlag(QStringList& args);

// Subcommand entry points. Each takes the post-subcommand argument
// list (after the subcommand name itself), plus the resolved socket
// path. Returns the process exit code:
//   0 - success
//   1 - usage error
//   2 - connection / I/O error
//   3 - server-side error (NO_SUCH_TARGET, etc.)
//   4 - internal setup failure (e.g. signal-handler install)
[[nodiscard]] int runCall(QStringList args, QString socketPath);
[[nodiscard]] int runList(QStringList args, QString socketPath);
[[nodiscard]] int runSchema(QStringList args, QString socketPath);
[[nodiscard]] int runSubscribe(QStringList args, QString socketPath);

} // namespace Phosphorctl
