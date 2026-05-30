// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorServiceNetwork.h
 * @brief Umbrella header for the PhosphorServiceNetwork library.
 *
 * PhosphorServiceNetwork exposes `org.freedesktop.NetworkManager` (system
 * bus) as Qt/QML-friendly types for Phosphor-based desktop shells. The
 * library has no UI of its own; shells consume:
 *   - `NetworkHost` — manager state (`connectivity`, `networkingEnabled`,
 *     `wirelessEnabled`) plus the write surface (`scanWifi()`,
 *     `activateConnection()`, `connectToAccessPoint()`).
 *   - `NetworkDevice` + `NetworkDeviceModel` — per-device rows.
 *   - `AccessPoint` + `AccessPointModel` — a Wi-Fi device's scanned
 *     networks (SSID / strength / security).
 *   - `NetworkConnection` + `NetworkConnectionModel` — saved profiles.
 * and render however they like.
 *
 * Phase 2.2 of the service-library plan documented in
 * `docs/phosphor-shell-design/04-implementation-plan.md`. Built on the
 * generic `PhosphorDBus::Client` async helper.
 */

#include <PhosphorServiceNetwork/AccessPoint.h>
#include <PhosphorServiceNetwork/AccessPointModel.h>
#include <PhosphorServiceNetwork/NetworkConnection.h>
#include <PhosphorServiceNetwork/NetworkConnectionModel.h>
#include <PhosphorServiceNetwork/NetworkDevice.h>
#include <PhosphorServiceNetwork/NetworkDeviceModel.h>
#include <PhosphorServiceNetwork/NetworkHost.h>
#include <PhosphorServiceNetwork/QmlRegistration.h>
