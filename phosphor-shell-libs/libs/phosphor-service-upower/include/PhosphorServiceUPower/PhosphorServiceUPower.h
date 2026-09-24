// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorServiceUPower.h
 * @brief Umbrella header for the PhosphorServiceUPower library.
 *
 * PhosphorServiceUPower exposes `org.freedesktop.UPower` (system bus)
 * as Qt/QML-friendly types (`UPowerHost`, `UPowerDevice`,
 * `UPowerDeviceModel`) for Phosphor-based desktop shells. The library
 * has no UI of its own; shells consume the host's `onBattery` /
 * `displayDevice` properties and the model's per-device rows and
 * render however they like.
 *
 * Extracted from the legacy `phosphor-services` umbrella as part of the
 * Phase 2.0 split documented in
 * `docs/phosphor-shell-design/04-implementation-plan.md`.
 */

#include <PhosphorServiceUPower/UPowerDevice.h>
#include <PhosphorServiceUPower/UPowerHost.h>
#include <PhosphorServiceUPower/UPowerDeviceModel.h>
#include <PhosphorServiceUPower/QmlRegistration.h>
