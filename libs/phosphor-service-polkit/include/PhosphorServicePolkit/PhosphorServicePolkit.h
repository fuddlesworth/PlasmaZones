// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorServicePolkit.h
 * @brief Umbrella header for the PhosphorServicePolkit library.
 *
 * PhosphorServicePolkit is a PolicyKit authentication agent for Phosphor-based
 * desktop shells, built on the `polkit-qt6` binding. When an application
 * requests a privileged action, `polkitd` calls into the registered agent,
 * which drives the PAM conversation that authenticates the user. The library
 * wraps polkit-qt privately, so its public surface is a clean Qt/QML type with
 * no polkit-qt types leaking out; it has no UI of its own (the authentication
 * dialog is a Phase 3 / 4 consumer).
 *
 * Shells consume:
 *   - `PolkitAgent`: registers as the session's authentication agent and
 *     surfaces the active request + a respond / cancel path.
 *   - `AuthRequest`: one decoded authentication request (action / message /
 *     icon / details / identities) polkit is waiting on.
 *
 * Phase 2.6 of the service-library plan documented in
 * `docs/phosphor-shell-design/04-implementation-plan.md`.
 */

#include <PhosphorServicePolkit/AuthRequest.h>
#include <PhosphorServicePolkit/PolkitAgent.h>
#include <PhosphorServicePolkit/QmlRegistration.h>
