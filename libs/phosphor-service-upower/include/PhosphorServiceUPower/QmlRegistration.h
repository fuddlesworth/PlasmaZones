// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceUPower/phosphorserviceupower_export.h>

namespace PhosphorServiceUPower {

/// Register every PhosphorServiceUPower QML type under the
/// `Phosphor.Service.UPower` module at version 1.0. Idempotent on
/// repeat calls: internally guarded by `std::call_once` so a hot-
/// reloading shell that builds a fresh `QQmlEngine` per reload can
/// safely call this from every engine setup without triggering Qt's
/// duplicate-registration warning.
///
/// Called from the consuming binary (typically `src/shell/main.cpp`
/// for the reference shell, but any QGuiApplication that wants to
/// expose UPower to its QML can call this) before any `QQmlEngine`
/// loads a `.qml` file. Keeping registration in the lib lets the
/// consumer stay free of the type-registration boilerplate while
/// still controlling *when* the types appear in their engine.
PHOSPHORSERVICEUPOWER_EXPORT void registerQmlTypes();

} // namespace PhosphorServiceUPower
