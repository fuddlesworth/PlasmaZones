// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServicePipeWire/phosphorservicepipewire_export.h>

namespace PhosphorServicePipeWire {

/// QML module identifiers. Consumers that need to refer to the module
/// URI (e.g. `qmlRegisterModule`, manual import statements, test
/// harnesses) should use these constants instead of string-literaling
/// `"Phosphor.Service.PipeWire"` so a future rename touches one site.
inline constexpr const char* kQmlModuleName = "Phosphor.Service.PipeWire";
inline constexpr int kQmlModuleMajor = 1;
inline constexpr int kQmlModuleMinor = 0;

/// Register every PhosphorServicePipeWire QML type under the
/// `Phosphor.Service.PipeWire` module at version 1.0. Idempotent on
/// repeat calls: internally guarded by `std::call_once` so a hot-
/// reloading shell that builds a fresh `QQmlEngine` per reload can
/// safely call this from every engine setup without triggering Qt's
/// duplicate-registration warning. Mirrors the sibling phosphor-
/// service-{sni,mpris,upower,icontheme} pattern.
///
/// Module-wide registration rationale: every public type in this
/// library (PipeWireConnection, PwNode, PwNodeModel,
/// PwSinkModel / PwSourceModel / PwStreamModel, PipeWireHost) is
/// deliberately registered imperatively from this function rather than
/// declaratively via `QML_ELEMENT` / `QML_SINGLETON` macros on each
/// class. The reasons are uniform across the surface:
///   - Hot-reloading shells construct a fresh `QQmlEngine` per reload
///     and the `std::call_once` guard above keeps registration
///     idempotent — `QML_ELEMENT`'s qmltyperegistrar-generated path
///     fires per-engine and would re-warn.
///   - The singleton factory needs a process-static `unique_ptr`
///     (PipeWireHost) so loop-thread ownership survives engine teardown.
///     `QML_SINGLETON` wires lifetime to the engine, which would tear
///     down the PipeWire main loop on every reload.
///   - The uncreatable types (PipeWireConnection, PwNode, PwNodeModel)
///     need bespoke "vended by …" messages and a single source of truth
///     for the module URI / version triple.
/// Keeping the rationale in one place (here) means any reader of an
/// individual header doesn't have to reconstruct the same explanation
/// from class-level comments scattered across the library.
PHOSPHORSERVICEPIPEWIRE_EXPORT void registerQmlTypes();

} // namespace PhosphorServicePipeWire
