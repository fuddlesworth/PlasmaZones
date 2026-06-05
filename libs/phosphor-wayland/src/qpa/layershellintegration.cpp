// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "layershellintegration.h"
#include "layershellwindow.h"
#include "../compositorlost_internal.h"
#include <PhosphorWayland/LayerSurface.h>

#include <algorithm>
#include <cstring>
#include <utility>
#include <QLoggingCategory>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#include <QtWaylandClient/private/qwaylandshellintegrationfactory_p.h>

Q_LOGGING_CATEGORY(lcLayerShellIntegration, "phosphorwayland.qpa.layershell")

namespace PhosphorWayland {

LayerShellIntegration* LayerShellIntegration::s_instance = nullptr;

static const struct wl_registry_listener s_registryListener = {
    .global = LayerShellIntegration::registryHandler,
    .global_remove = LayerShellIntegration::registryRemoveHandler,
};

LayerShellIntegration::LayerShellIntegration() = default;

LayerShellIntegration::~LayerShellIntegration()
{
    if (m_foreignToplevelManager) {
        // wlr-foreign-toplevel-management has no destroy request — the
        // protocol's lifecycle is: client sends stop() → compositor
        // responds with `finished` (a destructor event) → libwayland frees
        // the proxy. Calling wl_proxy_destroy ourselves between stop()
        // and the eventual `finished` is a use-after-free on whichever
        // side dispatches first. We send stop() and let the proxy be
        // reclaimed by libwayland's destructor handling for the
        // `finished` event; the wl_display teardown that runs
        // immediately after this destructor wipes everything anyway.
        //
        // `handleFinished` may have already called
        // clearForeignToplevelManager() — in that case
        // m_foreignToplevelManager is nullptr and we'd skip this
        // block entirely (the outer `if` checks it). If we're here,
        // `finished` hasn't fired yet, so the stop() is safe.
        zwlr_foreign_toplevel_manager_v1_stop(m_foreignToplevelManager);
        m_foreignToplevelManager = nullptr;
    }
    if (m_idleInhibitManager) {
        zwp_idle_inhibit_manager_v1_destroy(m_idleInhibitManager);
    }
    if (m_toplevelDragManager) {
        xdg_toplevel_drag_manager_v1_destroy(m_toplevelDragManager);
    }
    if (m_idleNotifier) {
        ext_idle_notifier_v1_destroy(m_idleNotifier);
    }
    if (m_singlePixelBufferManager) {
        wp_single_pixel_buffer_manager_v1_destroy(m_singlePixelBufferManager);
    }
    if (m_dataControlManager) {
        zwlr_data_control_manager_v1_destroy(m_dataControlManager);
    }
    if (m_sessionLockManager) {
        ext_session_lock_manager_v1_destroy(m_sessionLockManager);
    }
    if (m_layerShell) {
        if (m_boundVersion >= 3) {
            // Protocol-level destroy request (added in v3)
            zwlr_layer_shell_v1_destroy(m_layerShell);
        } else {
            // v1/v2 don't have a destroy request — clean up the client-side proxy
            // to avoid leaking the wl_proxy allocation.
            wl_proxy_destroy(reinterpret_cast<struct wl_proxy*>(m_layerShell));
        }
    }
    // Reap any stale layer-shell proxies parked when the compositor
    // re-advertised the global. We deferred their destruction at
    // re-advertise time because in-flight events may still have been
    // dispatching against them; by shutdown all dispatch has drained
    // and we can safely destroy.
    for (auto* p : m_staleProxies) {
        wl_proxy_destroy(p);
    }
    m_staleProxies.clear();
    if (m_registry) {
        wl_registry_destroy(m_registry);
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool LayerShellIntegration::initialize(QtWaylandClient::QWaylandDisplay* display)
{
    m_display = display;

    // Get wl_display and do a registry roundtrip to bind zwlr_layer_shell_v1
    struct wl_display* wlDisplay = display->wl_display();
    m_registry = wl_display_get_registry(wlDisplay);
    wl_registry_add_listener(m_registry, &s_registryListener, this);
    wl_display_roundtrip(wlDisplay);

    if (!m_layerShell) {
        qCWarning(lcLayerShellIntegration) << "Compositor does not support zwlr_layer_shell_v1 —"
                                           << "overlays will fall back to xdg_toplevel (wrong stacking/anchoring)."
                                           << "GNOME/Mutter does not implement this protocol.";
        return false;
    }

    // Set singleton only after successful initialization — if initialize() returns
    // false, Qt may destroy this object, and we must not leave a dangling s_instance.
    Q_ASSERT_X(!s_instance || s_instance == this, "LayerShellIntegration", "Double initialization detected");
    s_instance = this;

    qCInfo(lcLayerShellIntegration) << "Layer shell integration initialized";
    return true;
}

QtWaylandClient::QWaylandShellSurface*
LayerShellIntegration::createShellSurface(QtWaylandClient::QWaylandWindow* window)
{
    // Only create layer surfaces for windows we've marked
    QWindow* qwindow = window->window();
    if (!qwindow || !qwindow->property(LayerSurfaceProps::IsLayerShell).toBool()) {
        // Not a layer-shell window — delegate to xdg-shell so the window gets
        // a proper xdg_toplevel or xdg_popup role. Without this, the window has
        // no shell role and is invisible (Qt logs "Could not create a shell surface object").
        if (!m_xdgShell && m_display) {
            m_xdgShell.reset(
                QtWaylandClient::QWaylandShellIntegrationFactory::create(QStringLiteral("xdg-shell"), m_display));
            if (m_xdgShell) {
                qCDebug(lcLayerShellIntegration) << "Loaded xdg-shell fallback for regular windows";
            } else {
                qCWarning(lcLayerShellIntegration) << "Failed to load xdg-shell fallback";
            }
        }
        if (m_xdgShell) {
            return m_xdgShell->createShellSurface(window);
        }
        return nullptr;
    }

    if (!m_layerShell) {
        qCWarning(lcLayerShellIntegration) << "Cannot create layer surface: no zwlr_layer_shell_v1";
        return nullptr;
    }

    auto* shellWindow = new LayerShellWindow(this, window);
    if (!shellWindow->isValid()) {
        // Constructor failed to create a layer surface (wl_surface was null or
        // the global was removed between our check and the constructor). Delete
        // the zombie to avoid Qt calling methods on a half-initialized object.
        delete shellWindow;
        return nullptr;
    }
    return shellWindow;
}

void LayerShellIntegration::registryHandler(void* data, struct wl_registry* registry, uint32_t id,
                                            const char* interface, uint32_t version)
{
    auto* self = static_cast<LayerShellIntegration*>(data);
    if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        // Reject only if we already have a LIVE binding. After a global
        // removal (compositor restart), m_layerShell is left stale-but-
        // non-null because existing LayerShellWindow surfaces still
        // reference it; we keep it alive solely so the destructor can
        // wl_proxy_destroy it once those surfaces close. A re-advertised
        // global at that point is a legitimate rebind, not a duplicate.
        if (self->m_layerShell && self->m_globalAvailable) {
            qCDebug(lcLayerShellIntegration) << "Ignoring duplicate zwlr_layer_shell_v1 global (id" << id << ")";
            return;
        }
        // Maximum protocol version we bind:
        //   v1: base protocol
        //   v2: set_layer (runtime layer changes)
        //   v3: destroy request
        //   v4: on_demand keyboard interactivity
        //   v5: set_exclusive_edge
        static constexpr uint32_t kMaxVersion = 5;
        uint32_t bindVersion = qMin(version, kMaxVersion);
        if (self->m_layerShell) {
            // Stale proxy from a removed global. The compositor sends
            // closed events on the surfaces; existing LayerShellWindow
            // instances null their own m_layerSurface in handleClosed
            // and will naturally die on the next configure. Park the
            // stale proxy in m_staleProxies so the destructor can
            // destroy it cleanly at shutdown — without this, every
            // re-advertise leaks a wl_proxy plus its listener state
            // (bounded by re-advertise count but visible in valgrind
            // leak reports for long-running shells across compositor
            // restarts).
            qCInfo(lcLayerShellIntegration) << "Re-binding zwlr_layer_shell_v1 after global re-advertisement";
            self->m_staleProxies.push_back(reinterpret_cast<wl_proxy*>(self->m_layerShell));
            self->m_layerShell = nullptr;
        }
        self->m_layerShell = static_cast<struct zwlr_layer_shell_v1*>(
            wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, bindVersion));
        self->m_layerShellId = id;
        self->m_boundVersion = bindVersion;
        self->m_globalAvailable = true;
        qCDebug(lcLayerShellIntegration).nospace() << "Bound zwlr_layer_shell_v1 v" << bindVersion;
    } else if (strcmp(interface, wp_single_pixel_buffer_manager_v1_interface.name) == 0) {
        if (self->m_singlePixelBufferManager && self->m_singlePixelBufferAvailable)
            return;
        self->m_singlePixelBufferManager = nullptr;
        static constexpr uint32_t kMaxVersion = 1;
        uint32_t bindVersion = qMin(version, kMaxVersion);
        self->m_singlePixelBufferManager = static_cast<struct wp_single_pixel_buffer_manager_v1*>(
            wl_registry_bind(registry, id, &wp_single_pixel_buffer_manager_v1_interface, bindVersion));
        self->m_singlePixelBufferManagerId = id;
        self->m_singlePixelBufferAvailable = true;
        qCDebug(lcLayerShellIntegration).nospace() << "Bound wp_single_pixel_buffer_manager_v1 v" << bindVersion;
    } else if (strcmp(interface, ext_idle_notifier_v1_interface.name) == 0) {
        if (self->m_idleNotifier && self->m_idleNotifierAvailable)
            return;
        self->m_idleNotifier = nullptr;
        static constexpr uint32_t kMaxVersion = 1;
        uint32_t bindVersion = qMin(version, kMaxVersion);
        self->m_idleNotifier = static_cast<struct ext_idle_notifier_v1*>(
            wl_registry_bind(registry, id, &ext_idle_notifier_v1_interface, bindVersion));
        self->m_idleNotifierId = id;
        self->m_idleNotifierAvailable = true;
        qCDebug(lcLayerShellIntegration).nospace() << "Bound ext_idle_notifier_v1 v" << bindVersion;
    } else if (strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) == 0) {
        if (self->m_idleInhibitManager && self->m_idleInhibitManagerAvailable)
            return;
        self->m_idleInhibitManager = nullptr;
        static constexpr uint32_t kMaxVersion = 1;
        uint32_t bindVersion = qMin(version, kMaxVersion);
        self->m_idleInhibitManager = static_cast<struct zwp_idle_inhibit_manager_v1*>(
            wl_registry_bind(registry, id, &zwp_idle_inhibit_manager_v1_interface, bindVersion));
        self->m_idleInhibitManagerId = id;
        self->m_idleInhibitManagerAvailable = true;
        qCDebug(lcLayerShellIntegration).nospace() << "Bound zwp_idle_inhibit_manager_v1 v" << bindVersion;
    } else if (strcmp(interface, xdg_toplevel_drag_manager_v1_interface.name) == 0) {
        if (self->m_toplevelDragManager && self->m_toplevelDragManagerAvailable)
            return;
        self->m_toplevelDragManager = nullptr;
        static constexpr uint32_t kMaxVersion = 1;
        uint32_t bindVersion = qMin(version, kMaxVersion);
        self->m_toplevelDragManager = static_cast<struct xdg_toplevel_drag_manager_v1*>(
            wl_registry_bind(registry, id, &xdg_toplevel_drag_manager_v1_interface, bindVersion));
        self->m_toplevelDragManagerId = id;
        self->m_toplevelDragManagerAvailable = true;
        qCDebug(lcLayerShellIntegration).nospace() << "Bound xdg_toplevel_drag_manager_v1 v" << bindVersion;
    } else if (strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        if (self->m_foreignToplevelManager && self->m_foreignToplevelManagerAvailable)
            return;
        self->m_foreignToplevelManager = nullptr;
        // Maximum protocol version we bind:
        //   v1: base (title/app_id/state/output_enter/output_leave/done events;
        //       set_maximized/unset_maximized/set_minimized/unset_minimized/
        //       activate/close/set_rectangle requests)
        //   v2: set_fullscreen / unset_fullscreen
        //   v3: parent event
        static constexpr uint32_t kMaxVersion = 3;
        uint32_t bindVersion = qMin(version, kMaxVersion);
        self->m_foreignToplevelManager = static_cast<struct zwlr_foreign_toplevel_manager_v1*>(
            wl_registry_bind(registry, id, &zwlr_foreign_toplevel_manager_v1_interface, bindVersion));
        self->m_foreignToplevelManagerId = id;
        self->m_foreignToplevelManagerVersion = bindVersion;
        self->m_foreignToplevelManagerAvailable = true;
        qCDebug(lcLayerShellIntegration).nospace() << "Bound zwlr_foreign_toplevel_manager_v1 v" << bindVersion;
    } else if (strcmp(interface, zwlr_data_control_manager_v1_interface.name) == 0) {
        if (self->m_dataControlManager && self->m_dataControlManagerAvailable)
            return;
        self->m_dataControlManager = nullptr;
        // Bind v1: clipboard selection only. Primary-selection is the v2 feature
        // we deliberately do not consume.
        static constexpr uint32_t kMaxVersion = 1;
        uint32_t bindVersion = qMin(version, kMaxVersion);
        self->m_dataControlManager = static_cast<struct zwlr_data_control_manager_v1*>(
            wl_registry_bind(registry, id, &zwlr_data_control_manager_v1_interface, bindVersion));
        self->m_dataControlManagerId = id;
        self->m_dataControlManagerAvailable = true;
        qCDebug(lcLayerShellIntegration).nospace() << "Bound zwlr_data_control_manager_v1 v" << bindVersion;
    } else if (strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
        if (self->m_sessionLockManager && self->m_sessionLockManagerAvailable)
            return;
        self->m_sessionLockManager = nullptr;
        static constexpr uint32_t kMaxVersion = 1;
        uint32_t bindVersion = qMin(version, kMaxVersion);
        self->m_sessionLockManager = static_cast<struct ext_session_lock_manager_v1*>(
            wl_registry_bind(registry, id, &ext_session_lock_manager_v1_interface, bindVersion));
        self->m_sessionLockManagerId = id;
        self->m_sessionLockManagerAvailable = true;
        qCDebug(lcLayerShellIntegration).nospace() << "Bound ext_session_lock_manager_v1 v" << bindVersion;
    }
}

void LayerShellIntegration::registryRemoveHandler(void* data, struct wl_registry* registry, uint32_t id)
{
    Q_UNUSED(registry)
    auto* self = static_cast<LayerShellIntegration*>(data);
    if (id == self->m_singlePixelBufferManagerId) {
        self->m_singlePixelBufferAvailable = false;
        self->m_singlePixelBufferManagerId = 0;
        qCDebug(lcLayerShellIntegration) << "wp_single_pixel_buffer_manager_v1 global removed";
    } else if (id == self->m_idleNotifierId) {
        self->m_idleNotifierAvailable = false;
        self->m_idleNotifierId = 0;
        qCDebug(lcLayerShellIntegration) << "ext_idle_notifier_v1 global removed";
    } else if (id == self->m_idleInhibitManagerId) {
        self->m_idleInhibitManagerAvailable = false;
        self->m_idleInhibitManagerId = 0;
        qCDebug(lcLayerShellIntegration) << "zwp_idle_inhibit_manager_v1 global removed";
    } else if (id == self->m_toplevelDragManagerId) {
        self->m_toplevelDragManagerAvailable = false;
        self->m_toplevelDragManagerId = 0;
        qCDebug(lcLayerShellIntegration) << "xdg_toplevel_drag_manager_v1 global removed";
    } else if (id == self->m_foreignToplevelManagerId) {
        self->m_foreignToplevelManagerAvailable = false;
        self->m_foreignToplevelManagerId = 0;
        qCDebug(lcLayerShellIntegration) << "zwlr_foreign_toplevel_manager_v1 global removed";
    } else if (id == self->m_dataControlManagerId) {
        self->m_dataControlManagerAvailable = false;
        self->m_dataControlManagerId = 0;
        qCDebug(lcLayerShellIntegration) << "zwlr_data_control_manager_v1 global removed";
    } else if (id == self->m_sessionLockManagerId) {
        self->m_sessionLockManagerAvailable = false;
        self->m_sessionLockManagerId = 0;
        qCDebug(lcLayerShellIntegration) << "ext_session_lock_manager_v1 global removed";
    } else if (id == self->m_layerShellId) {
        qCWarning(lcLayerShellIntegration) << "zwlr_layer_shell_v1 global removed (id" << id << ") —"
                                           << "new layer surface creation will fail."
                                           << "Existing layer surfaces may become invalid."
                                           << "If the compositor crashed, these surfaces are now stale.";
        // Do NOT destroy the global object here — existing LayerShellWindow
        // instances hold zwlr_layer_surface_v1 objects created from this global.
        // Destroying the global while child surfaces are alive causes UAF.
        // The compositor removing the global means it is shutting down or
        // reconfiguring; the layer surfaces will be invalidated by the compositor
        // and receive closed events. We mark the global as unavailable to prevent
        // new surface creation, but keep m_layerShell and m_boundVersion intact
        // so the destructor can properly clean up the wl_proxy binding.
        self->m_globalAvailable = false;
        self->m_layerShellId = 0;

        // Notify any active LayerShellWindow instances that the global is gone.
        // Their zwlr_layer_surface_v1 objects are now stale — the compositor will
        // send closed events, but nulling m_layerSurface proactively prevents us
        // from issuing protocol requests on dead objects in the interim.
        self->fireGlobalRemovedCallbacks();
    }
}

void LayerShellIntegration::fireGlobalRemovedCallbacks()
{
    auto callbacks = std::exchange(m_globalRemovedCallbacks, {});
    for (const auto& [cbId, cb] : callbacks) {
        cb();
    }
    // Forward the same edge to the public CompositorLost broadcaster so
    // out-of-tree consumers (PhosphorLayer's PhosphorWaylandTransport, app-
    // level subscribers) react to mid-session compositor crashes without
    // depending on QGuiApplication::aboutToQuit. The broadcaster fires at
    // most once per process and tolerates the duplicate emit if a future
    // path also reaches it.
    fireCompositorLost();
}

LayerShellIntegration::CallbackId LayerShellIntegration::addGlobalRemovedCallback(GlobalRemovedCallback cb)
{
    CallbackId id = m_nextCallbackId++;
    m_globalRemovedCallbacks.push_back({id, std::move(cb)});
    return id;
}

void LayerShellIntegration::removeGlobalRemovedCallback(CallbackId id)
{
    m_globalRemovedCallbacks.erase(std::remove_if(m_globalRemovedCallbacks.begin(), m_globalRemovedCallbacks.end(),
                                                  [id](const auto& entry) {
                                                      return entry.first == id;
                                                  }),
                                   m_globalRemovedCallbacks.end());
}

} // namespace PhosphorWayland
