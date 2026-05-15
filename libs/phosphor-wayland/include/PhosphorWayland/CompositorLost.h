// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstdint>
#include <functional>

#include <phosphorwayland_export.h>

namespace PhosphorWayland {

/// Public seam over the QPA plugin's wlr-layer-shell global-removal signal.
///
/// The QPA plugin observes `wl_registry::global_remove` for
/// `zwlr_layer_shell_v1` and uses that to invalidate its cached binding when
/// the compositor restarts or stops. This header forwards the same edge to
/// process-wide consumers: anyone holding `LayerSurface` instances or built-on
/// state (e.g. `PhosphorLayer::PhosphorWaylandTransport`) needs to drop them
/// when the global is gone, otherwise subsequent protocol requests target a
/// dead binding.
///
/// Semantics
/// - Fires at most once per process. After the first fire, the broadcaster
///   stays in the fired state and any later registration invokes the new
///   callback synchronously.
/// - Callbacks fire on the Wayland event-dispatch thread (Qt's GUI thread for
///   the standard QGuiApplication setup); marshal to your own thread if the
///   work isn't safe there.
/// - Registration is valid before the QPA plugin is initialised. The
///   broadcaster is independent of the integration singleton; the integration
///   wires its own forwarder during `initialize()` so callbacks registered
///   pre-load are still reached when the compositor signals removal later.
///
/// Cleanup
/// Long-lived consumers MUST `removeCompositorLostCallback()` before the
/// callback's captures become invalid. Cookies are monotonic and never reused
/// within a process; cookie 0 is reserved for "registration refused" (null
/// callback) and is safe to pass to remove.
using CompositorLostCallback = std::function<void()>;
using CompositorLostCookie = std::uint64_t;

/// Register a callback fired when the compositor removes the
/// `zwlr_layer_shell_v1` global. Returns a non-zero cookie on success; 0 if
/// `cb` is null. If the global has already been removed, `cb` is invoked
/// synchronously before this function returns and the returned cookie is
/// inert.
[[nodiscard]] PHOSPHORWAYLAND_EXPORT CompositorLostCookie addCompositorLostCallback(CompositorLostCallback cb);

/// Unregister a callback previously returned by `addCompositorLostCallback`.
/// Safe with `cookie == 0` and with cookies whose callbacks have already
/// fired (no-op).
PHOSPHORWAYLAND_EXPORT void removeCompositorLostCallback(CompositorLostCookie cookie);

} // namespace PhosphorWayland
