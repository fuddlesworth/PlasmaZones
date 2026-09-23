// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

namespace PhosphorWayland {

/// Internal entry point invoked by the QPA plugin when the compositor
/// removes the zwlr_layer_shell_v1 global. Idempotent (the broadcaster
/// fires at most once per process). Lives in a private header so external
/// callers can't synthesise the event.
void fireCompositorLost();

} // namespace PhosphorWayland
