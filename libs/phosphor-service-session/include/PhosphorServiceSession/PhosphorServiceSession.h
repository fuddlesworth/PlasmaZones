// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorServiceSession.h
 * @brief Umbrella header for the PhosphorServiceSession library.
 *
 * PhosphorServiceSession is the logind session manager for Phosphor-based
 * desktop shells. It surfaces the system session and power actions (lock,
 * logout, suspend, hibernate, hybrid-sleep, suspend-then-hibernate, reboot,
 * power-off, halt) over `org.freedesktop.login1.Manager`, each gated by its
 * capability query (`CanSuspend` and siblings) read up front. It manages
 * logind inhibitor locks so the shell can lock before the machine sleeps and
 * can own the power / suspend / hibernate / lid keys, and it surfaces logind's
 * session and sleep signals. The library has no UI of its own.
 *
 * Because we own the compositor and the session, this is a session manager
 * rather than a thin action wrapper: a delay inhibitor on `sleep` lets the
 * shell raise the lock surface before suspend proceeds (the lock-before-sleep
 * handshake with `phosphor-service-lock`), which a plugin shell cannot do.
 *
 * Shells consume `SessionHost` and render the power menu however they like.
 *
 * Phase 2.10 of the service-library plan documented in
 * `docs/phosphor-shell-design/04-implementation-plan.md`. Built on the generic
 * `PhosphorDBus::Client` async helper for the logind calls.
 */

#include <PhosphorServiceSession/QmlRegistration.h>
#include <PhosphorServiceSession/SessionHost.h>
