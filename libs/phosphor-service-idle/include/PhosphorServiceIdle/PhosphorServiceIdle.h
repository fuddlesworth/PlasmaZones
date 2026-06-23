// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorServiceIdle.h
 * @brief Umbrella header for the PhosphorServiceIdle library.
 *
 * PhosphorServiceIdle is a Wayland idle-management service for Phosphor-based
 * desktop shells. It watches the session for inactivity through a configurable
 * multi-stage timeout policy and inhibits idle on request. It is the policy
 * layer over the raw protocol clients that already live in `phosphor-wayland`
 * (`IdleNotifier` for `ext-idle-notify-v1`, `IdleInhibitor` for
 * `zwp-idle-inhibit-v1`), composing them rather than binding the protocols
 * itself, so its public surface is a clean Qt/QML type with no Wayland types
 * leaking out.
 *
 * Shells consume:
 *   - `IdleService`: the session idle host (current state, configured stages,
 *     an inhibit path).
 *
 * Phase 2.7 of the service-library plan documented in
 * `docs/phosphor-shell-design/04-implementation-plan.md`.
 */

#include <PhosphorServiceIdle/IdleService.h>
#include <PhosphorServiceIdle/QmlRegistration.h>
