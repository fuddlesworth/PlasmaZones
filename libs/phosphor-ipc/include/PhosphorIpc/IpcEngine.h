// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorIpc/phosphoripc_export.h>

#include <QtCore/qtmetamacros.h> // QT_BEGIN_NAMESPACE / QT_END_NAMESPACE

QT_BEGIN_NAMESPACE
class QQmlEngine;
QT_END_NAMESPACE

namespace PhosphorIpc {

class IpcRouter;

// Bridge between the application's IpcRouter (constructed in main())
// and QML-side IpcTarget instances (instantiated by the engine).
// The router is stored as a dynamic property on the engine
// ("phosphorIpcRouter"); IpcTarget::componentComplete() reads it
// back via qmlEngine(this)->property(...).
//
// Why a free function rather than a QML singleton or context
// property: the router's lifetime is owned by main(), not the
// engine, so QML_SINGLETON's lazy per-engine construction model
// doesn't fit. setContextProperty would work but pollutes the QML
// root namespace; an opaque engine dynamic property keeps the
// integration invisible to surface QML.
//
// Idempotent: calling install() twice with the same router is a
// no-op; calling with a different router on the same engine logs
// a qWarning and replaces the binding (test-environment use case).
//
// Thread affinity: install(), uninstall(), and routerFor() must be
// called on the engine's owning thread (the GUI thread in typical
// usage). QQmlEngine is thread-affine, and the bookkeeping globals
// inside ipcengine.cpp are not synchronised; an off-thread call is
// asserted against in debug builds.
namespace IpcEngine {

PHOSPHORIPC_EXPORT void install(QQmlEngine* engine, IpcRouter* router);

// Inverse of install(). Clears the engine's router property.
// Called from main() during shutdown if the application wants to
// drop the binding before the engine is destroyed (the typical
// reverse-order destruction handles it implicitly).
PHOSPHORIPC_EXPORT void uninstall(QQmlEngine* engine);

// Resolve the router previously installed on this engine. Returns
// nullptr if no install() has been called for this engine.
[[nodiscard]] PHOSPHORIPC_EXPORT IpcRouter* routerFor(QQmlEngine* engine);

} // namespace IpcEngine

} // namespace PhosphorIpc
