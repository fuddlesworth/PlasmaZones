// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorServicePipeWire.h
 * @brief Umbrella header for the PhosphorServicePipeWire library.
 *
 * Native PipeWire mixer surface (sinks / sources / streams,
 * default-node switching, per-app volume + mute) for Phosphor-based
 * desktop shells. Phase 2.1 of `docs/phosphor-shell-design/04-
 * implementation-plan.md`.
 *
 * The library is non-visual; QML consumers bind to the host singleton's
 * connection state and the (forthcoming) sink / source / stream models
 * and render however they like. The PipeWire main loop runs on a
 * dedicated `QThread` inside the same `QGuiApplication` that hosts the
 * shell (U3 resolution: single-process); all cross-thread communication
 * routes through Qt's queued-signal infrastructure so consumers see a
 * pure GUI-thread API.
 */

#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PwNode.h>
#include <PhosphorServicePipeWire/QmlRegistration.h>
