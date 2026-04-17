// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class QJSEngine;
class QString;

namespace PhosphorTiles {

/**
 * @brief Harden a QJSEngine sandbox for safe user-script execution
 *
 * Applies all sandbox hardening steps in order:
 * 1. Freeze built-in helper globals (applyTreeGeometry, etc.)
 * 2. Lock down eval(), Function, GeneratorFunction, AsyncFunction constructors
 * 3. Freeze Object.prototype and Array.prototype (fatal on failure)
 * 4. Close Object.constructor escape on all major built-in objects
 * 5. Disable Proxy, Reflect, WeakRef, FinalizationRegistry
 * 6. Strip dangerous QJSEngine-provided globals (Qt, print, console, gc, timers)
 *
 * @param engine The QJSEngine to harden (must not be null)
 * @return true if all critical hardening steps succeeded, false if compromised
 */
bool hardenSandbox(QJSEngine* engine);

} // namespace PhosphorTiles
