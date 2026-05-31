// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorServiceBrightness.h
 * @brief Umbrella header for the PhosphorServiceBrightness library.
 *
 * PhosphorServiceBrightness exposes display and keyboard backlight brightness
 * as Qt/QML-friendly types for Phosphor-based desktop shells. The library has
 * no UI of its own. The read path is sysfs (`/sys/class/backlight` and the
 * `*::kbd_backlight` entries under `/sys/class/leds`); the write path is
 * logind's `org.freedesktop.login1.Session.SetBrightness`, so no root or udev
 * rules are required.
 *
 * Shells consume:
 *   - `BrightnessHost`: enumerates the brightness-controllable devices.
 *   - `BrightnessDevice` + `BrightnessDeviceModel`: per-device state
 *     (brightness / maxBrightness / percentage / kind) and the set surface.
 * and render however they like.
 *
 * Phase 2.4 of the service-library plan documented in
 * `docs/phosphor-shell-design/04-implementation-plan.md`. Built on the generic
 * `PhosphorDBus::Client` async helper for the logind call.
 */

#include <PhosphorServiceBrightness/BrightnessDevice.h>
#include <PhosphorServiceBrightness/BrightnessDeviceModel.h>
#include <PhosphorServiceBrightness/BrightnessHost.h>
#include <PhosphorServiceBrightness/QmlRegistration.h>
