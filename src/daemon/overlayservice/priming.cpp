// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Surface render-pipeline prime / cancel-prime helpers. Wires a
// fire-once frameSwapped-driven hide for newly-created surfaces so the
// Vulkan swapchain + QML scene-graph have walked at least one frame
// before the user-triggered show path takes over.

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"

#include <PhosphorLayer/Surface.h>

#include <QPointer>
#include <QQuickWindow>

#include <memory>

namespace PlasmaZones {

void OverlayService::primeSurfaceRenderPipeline(PhosphorLayer::Surface* surface)
{
    if (!surface) {
        return;
    }
    // contains-check covers BOTH lifecycle stages of a single prime
    // (pending warm + window-armed): once a surface is in the set, no
    // path adds another stateChanged or frameSwapped lambda to it.
    // Without this gate, an external double-call to
    // primeSurfaceRenderPipeline (e.g. show path that races a screen
    // reconfigure) would arm a second frameSwapped connection - one
    // would fire and hide the surface mid-content.
    if (m_primingSurfaces.contains(surface)) {
        return;
    }

    // Single destroyed-cleanup per surface (per OverlayService
    // instance), tracked in m_primingDestroyedConnections. Replaces
    // an earlier per-surface dynamic-property
    // gate which leaked across service instances - a fresh service
    // re-encountering the same Surface* would skip wiring its own
    // cleanup. The slot's static_cast on `dying` is safe because the
    // resulting pointer is only used as a hash-map key (compare-by-
    // address); ~QObject has already run by the time destroyed fires.
    if (!m_primingDestroyedConnections.contains(surface)) {
        QMetaObject::Connection destroyedConn = connect(surface, &QObject::destroyed, this, [this](QObject* dying) {
            auto* surf = static_cast<PhosphorLayer::Surface*>(dying);
            m_primingSurfaces.remove(surf);
            m_primingFrameConnections.remove(surf);
            m_primingDestroyedConnections.remove(surf);
        });
        m_primingDestroyedConnections.insert(surface, destroyedConn);
    }

    auto* window = surface->window();
    if (!window) {
        // Surface hasn't materialised a QQuickWindow yet - Surface::warmUp
        // is asynchronous for content that compiles off the main thread.
        // Defer until warm completes (stateChanged Warming → Hidden).
        // Disconnect on the FIRST Hidden - even if the window is somehow
        // still null we drop the connection rather than letting it stay
        // armed forever and re-fire on every later state change. The
        // recursive call lands in the window-non-null branch which adds
        // to m_primingSurfaces and installs the frameSwapped path.
        //
        // Insert into m_primingSurfaces NOW (warm-pending sentinel) so
        // an external second call to primeSurfaceRenderPipeline before
        // the first warm completes hits the contains() guard above and
        // bails - without this, the second call would queue a SECOND
        // stateChanged lambda whose recursive call lands in the window-
        // path's contains() bail at line `m_primingSurfaces.contains
        // (surface) → return` after the first one already inserted +
        // armed, leaking the second stateChanged slot for the rest of
        // the surface's lifetime.
        //
        // Disconnects on Hidden OR Failed: a surface stuck in Failed
        // never reaches Hidden, so without the Failed branch the
        // sentinel sits in m_primingSurfaces indefinitely, blocking
        // any future re-prime even after a recovery path puts the
        // surface back into a usable state.
        m_primingSurfaces.insert(surface);
        QPointer<PhosphorLayer::Surface> guard(surface);
        auto warmConn = std::make_shared<QMetaObject::Connection>();
        *warmConn = connect(surface, &PhosphorLayer::Surface::stateChanged, this,
                            [this, guard, warmConn](PhosphorLayer::Surface::State newState) {
                                if (newState != PhosphorLayer::Surface::State::Hidden
                                    && newState != PhosphorLayer::Surface::State::Failed) {
                                    return;
                                }
                                QObject::disconnect(*warmConn);
                                if (!guard) {
                                    return;
                                }
                                // Drop the warm-pending sentinel BEFORE the
                                // recursive call so the window-path's
                                // contains() guard re-evaluates to false
                                // and proceeds to insert + arm the
                                // frameSwapped handler.
                                //
                                // CRITICAL: gate the recursive prime on
                                // `m_primingSurfaces.remove(...)` returning
                                // true. If a user-show called
                                // cancelSurfacePrime during the warm-pending
                                // window, the surface was already removed
                                // from the set, `remove()` returns false,
                                // and we MUST NOT recurse - recursion would
                                // re-arm a fresh prime cycle whose
                                // frameSwapped-driven hide() races the
                                // user's just-shown content off the screen.
                                // Without this guard, cancelSurfacePrime is
                                // a silent no-op while the warm hasn't
                                // completed.
                                const bool stillPriming = m_primingSurfaces.remove(guard.data());
                                if (stillPriming && newState == PhosphorLayer::Surface::State::Hidden
                                    && guard->window() != nullptr) {
                                    primeSurfaceRenderPipeline(guard.data());
                                }
                            });
        return;
    }
    m_primingSurfaces.insert(surface);

    // Hide on the first frameSwapped after surface->show(). By that
    // point the wl_surface is mapped, the Vulkan swapchain has at
    // least one image, and the QML scene-graph (including any
    // QSGLayer that the shader path will later use) has rendered
    // at least one frame.
    //
    // The connection is tracked in m_primingFrameConnections so
    // cancelSurfacePrime can disconnect it explicitly - without
    // tracking, the connection survives until next paint and we
    // accumulate one stale slot per prime cycle for the surface's
    // lifetime under rapid show/hide.
    QPointer<PhosphorLayer::Surface> guard(surface);
    QMetaObject::Connection frameConn = connect(window, &QQuickWindow::frameSwapped, this, [this, guard]() {
        if (!guard) {
            // Surface died after the connection was armed but before
            // first frameSwapped - the destroyed-signal lambda in
            // m_primingDestroyedConnections has already cleaned the
            // map entry, and Qt's sender-destruction auto-disconnect
            // (window dies with surface) will retire this lambda
            // shortly. Nothing to do here.
            return;
        }
        const auto connIt = m_primingFrameConnections.find(guard.data());
        if (connIt != m_primingFrameConnections.end()) {
            QObject::disconnect(connIt.value());
            m_primingFrameConnections.erase(connIt);
        }
        // Only hide if the user hasn't already taken over the surface
        // (cancelSurfacePrime would have removed us from the set).
        if (m_primingSurfaces.remove(guard.data())) {
            guard->hide();
        }
    });
    m_primingFrameConnections.insert(surface, frameConn);
    surface->show();
}

void OverlayService::cancelSurfacePrime(PhosphorLayer::Surface* surface)
{
    // Idempotent - called from every user show path so a non-priming
    // surface short-circuits cheaply. Disconnect the frameSwapped
    // lambda EXPLICITLY (tracked in m_primingFrameConnections) so the
    // queued hide-on-first-paint never fires after a user-show. The
    // m_primingSurfaces.remove() is the secondary guard the lambda
    // would also check, but explicit disconnection is the safer
    // primary contract - any future event-loop pump between cancel
    // and the user's surface->show() is now harmless. Surfaces that
    // get torn down outside of cancelSurfacePrime are cleaned via the
    // destroyed signal connection in m_primingDestroyedConnections.
    m_primingSurfaces.remove(surface);
    const auto connIt = m_primingFrameConnections.find(surface);
    if (connIt != m_primingFrameConnections.end()) {
        QObject::disconnect(connIt.value());
        m_primingFrameConnections.erase(connIt);
    }
}

} // namespace PlasmaZones
