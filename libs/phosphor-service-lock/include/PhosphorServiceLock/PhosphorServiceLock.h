// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorServiceLock.h
 * @brief Umbrella header for the PhosphorServiceLock library.
 *
 * PhosphorServiceLock is a session-lock + authentication service for
 * Phosphor-based desktop shells. It authenticates the user through PAM and
 * coordinates the `ext-session-lock-v1` lock state with the compositor through
 * `phosphor-wayland`'s `SessionLock` client. It is the policy layer over the raw
 * protocol client, composing it rather than binding the protocol itself, so its
 * public surface is a clean Qt/QML type with no Wayland (or PAM) types leaking
 * out.
 *
 * Shells consume:
 *   - `LockService`: the lock host (lock state machine + authenticate path).
 *   - `PamAuthenticator` / `IAuthenticator`: the standalone credential-check
 *     surface, for verifying the user's password independently of the lock.
 *
 * Lock *surfaces* (the per-output graphics shown while locked) are a shell
 * concern wired in a later phase; this service owns authentication and the lock
 * lifecycle, not rendering.
 *
 * Phase 2.9 of the service-library plan documented in
 * `docs/phosphor-shell-design/04-implementation-plan.md`.
 */

#include <PhosphorServiceLock/IAuthenticator.h>
#include <PhosphorServiceLock/LockService.h>
#include <PhosphorServiceLock/PamAuthenticator.h>
#include <PhosphorServiceLock/QmlRegistration.h>
