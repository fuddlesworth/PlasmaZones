// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorServiceClipboard.h
 * @brief Umbrella header for the PhosphorServiceClipboard library.
 *
 * PhosphorServiceClipboard is a clipboard-history service for Phosphor-based
 * desktop shells. It watches the session clipboard through `phosphor-wayland`'s
 * `ClipboardDevice` (a `wlr-data-control` client), keeps a de-duplicated, capped,
 * on-disk history, and can re-apply any entry. It is the policy / history /
 * persistence layer over the raw device, composing it rather than binding the
 * protocol itself, so its public surface is a clean Qt/QML type with no Wayland
 * types leaking out.
 *
 * Shells consume:
 *   - `ClipboardService`: the clipboard-history host (the live history model and
 *     a copy / remove / clear path).
 *
 * Phase 2.8 of the service-library plan documented in
 * `docs/phosphor-shell-design/04-implementation-plan.md`.
 */

#include <PhosphorServiceClipboard/ClipboardService.h>
#include <PhosphorServiceClipboard/QmlRegistration.h>
