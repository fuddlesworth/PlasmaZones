// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorServiceNetwork.h
 * @brief Umbrella header for the PhosphorServiceNetwork library.
 *
 * PhosphorServiceNetwork exposes `org.freedesktop.NetworkManager` (system
 * bus) as Qt/QML-friendly types (`NetworkHost`, `NetworkDevice`,
 * `NetworkDeviceModel`) for Phosphor-based desktop shells. The library has
 * no UI of its own; shells consume the host's `connectivity` /
 * `networkingEnabled` / `wirelessEnabled` properties and the model's
 * per-device rows and render however they like.
 *
 * Phase 2.2 of the service-library plan documented in
 * `docs/phosphor-shell-design/04-implementation-plan.md`. Built on the
 * generic `PhosphorDBus::Client` async helper.
 */

#include <PhosphorServiceNetwork/AccessPoint.h>
#include <PhosphorServiceNetwork/AccessPointModel.h>
#include <PhosphorServiceNetwork/NetworkDevice.h>
#include <PhosphorServiceNetwork/NetworkHost.h>
#include <PhosphorServiceNetwork/NetworkDeviceModel.h>
#include <PhosphorServiceNetwork/QmlRegistration.h>
