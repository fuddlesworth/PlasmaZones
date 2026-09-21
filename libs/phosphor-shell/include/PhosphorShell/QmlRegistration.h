// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

namespace PhosphorShell {

/// Register every PhosphorShell QML type under the `Phosphor.Shell` module at
/// version 1.0. Idempotent on repeat calls: internally guarded by
/// `std::call_once`, because Qt's type registry is process-global rather than
/// per-engine, so several `ShellEngine`s in one process (sequential tests, a
/// future multi-shell daemon) must not trip the duplicate-registration warning.
///
/// `ShellEngine::load()` calls this, so a shell built the ordinary way needs
/// nothing. It is exposed for the same reason every phosphor-service library
/// exposes one: a consumer that drives a bare `QQmlEngine` — a QtQuickTest
/// runner, a tool — can register the types without standing up a whole
/// ShellEngine and the screen provider it depends on. Call it before any
/// engine loads a `.qml` file.
PHOSPHORSHELL_EXPORT void registerQmlTypes();

} // namespace PhosphorShell
