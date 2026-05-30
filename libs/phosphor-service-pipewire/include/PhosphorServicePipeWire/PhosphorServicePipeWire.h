// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorServicePipeWire.h
 * @brief Umbrella header for the PhosphorServicePipeWire library.
 *
 * Native PipeWire mixer surface (sinks / sources / streams,
 * default-node switching, per-app volume + mute) for Phosphor-based
 * desktop shells. Phase 2.1 of `docs/phosphor-shell-design/04-implementation-plan.md`.
 *
 * The library is non-visual; QML consumers bind to the host singleton's
 * connection state and the (forthcoming) sink / source / stream models
 * and render however they like. The PipeWire main loop runs on a
 * dedicated `QThread` inside the hosting `QCoreApplication` (a
 * `QGuiApplication` works equally well — the library only requires
 * `QCoreApplication` since it is non-visual) (U3 resolution:
 * single-process); all cross-thread communication routes through Qt's
 * queued-signal infrastructure so consumers see a pure GUI-thread API.
 *
 * Consumers wanting C++-only access (e.g. unit tests, headless
 * services that never spin up a `QQmlEngine`) can include individual
 * headers directly — `PipeWireConnection.h`, `PwNode.h`, etc. — and
 * skip this umbrella entirely. The umbrella additionally pulls in
 * `QmlRegistration.h`, which is only useful when you intend to call
 * `registerQmlTypes()` for a QML engine.
 */

#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PipeWireHost.h>
#include <PhosphorServicePipeWire/PwNode.h>
#include <PhosphorServicePipeWire/PwNodeModel.h>
#include <PhosphorServicePipeWire/QmlRegistration.h>
