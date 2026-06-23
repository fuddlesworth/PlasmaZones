// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorServiceBluetooth.h
 * @brief Umbrella header for the PhosphorServiceBluetooth library.
 *
 * PhosphorServiceBluetooth exposes `org.bluez` (system bus) as Qt/QML-
 * friendly types for Phosphor-based desktop shells. The library has no UI
 * of its own. BlueZ is ObjectManager-rooted, so the library is built on
 * `PhosphorDBus::ObjectManager` for adapter / device enumeration and acts
 * as a D-Bus server for the pairing agent (`org.bluez.Agent1`).
 *
 * Shells consume:
 *   - `BluetoothHost`: the facade for adapter / device enumeration plus the
 *     pairing-agent request signals.
 *   - `BluetoothAdapter` + `BluetoothAdapterModel`: per-adapter state and
 *     the discovery / power write surface.
 *   - `BluetoothDevice` + `BluetoothDeviceModel`: per-device state and the
 *     pair / connect / trust write surface.
 * and render however they like.
 *
 * Phase 2.3 of the service-library plan documented in
 * `docs/phosphor-shell-design/04-implementation-plan.md`. Built on the
 * generic `PhosphorDBus::Client` async helper and `PhosphorDBus::ObjectManager`.
 */

#include <PhosphorServiceBluetooth/BluetoothAdapter.h>
#include <PhosphorServiceBluetooth/BluetoothAdapterModel.h>
#include <PhosphorServiceBluetooth/BluetoothAgent.h>
#include <PhosphorServiceBluetooth/BluetoothDevice.h>
#include <PhosphorServiceBluetooth/BluetoothDeviceModel.h>
#include <PhosphorServiceBluetooth/BluetoothHost.h>
#include <PhosphorServiceBluetooth/QmlRegistration.h>
