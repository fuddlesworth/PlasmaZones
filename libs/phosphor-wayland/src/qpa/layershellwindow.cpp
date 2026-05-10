// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "layershellwindow.h"
#include "layershellintegration.h"
#include <PhosphorWayland/LayerSurface.h>

#include <any>

#include <QLoggingCategory>
#include <qpa/qwindowsysteminterface.h>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandscreen_p.h>
#include <QtWaylandClient/private/qwaylandshellintegration_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

Q_LOGGING_CATEGORY(lcLayerShellWindow, "phosphorwayland.qpa.layershellwindow")

namespace PhosphorWayland {

static const struct zwlr_layer_surface_v1_listener s_layerSurfaceListener = {
    .configure = LayerShellWindow::handleConfigure,
    .closed = LayerShellWindow::handleClosed,
};

LayerShellWindow::LayerShellWindow(LayerShellIntegration* integration, QtWaylandClient::QWaylandWindow* waylandWindow)
    : QWaylandShellSurface(waylandWindow)
    , m_integration(integration)
    , m_waylandWindow(waylandWindow)
{
    QWindow* qwindow = waylandWindow->window();

    // Read initial properties from QWindow dynamic properties
    int layer = qwindow->property(LayerSurfaceProps::Layer).toInt();
    // Scope default ("phosphorwayland") is set by LayerSurface::get() — if still empty
    // here, the caller explicitly wants no scope (or forgot to set one).
    QString scope = qwindow->property(LayerSurfaceProps::Scope).toString();

    // Create the layer surface
    m_wlSurface = QtWaylandClient::QWaylandShellIntegration::wlSurfaceForWindow(waylandWindow);
    if (!m_wlSurface) {
        qCCritical(lcLayerShellWindow) << "wlSurfaceForWindow returned null — cannot create layer surface";
        return;
    }
    // Guard against TOCTOU race: the global may have been removed between
    // createShellSurface()'s null check and this point (e.g. compositor crash).
    struct zwlr_layer_shell_v1* shell = integration->layerShell();
    if (!shell) {
        qCCritical(lcLayerShellWindow) << "zwlr_layer_shell_v1 global removed between createShellSurface()"
                                       << "and LayerShellWindow constructor — cannot create layer surface";
        return;
    }

    // Resolve wl_output locally — only needed for zwlr_layer_shell_v1_get_layer_surface().
    // Not stored as a member since it is not referenced after construction.
    // If the screen was hot-unplugged between LayerSurface::get() and now,
    // screen() returns nullptr and we pass NULL to the compositor (which then
    // chooses the output — usually primary).
    struct wl_output* output = nullptr;
    QScreen* targetScreen = qwindow->screen();
    if (targetScreen) {
        auto* waylandScreen = dynamic_cast<QtWaylandClient::QWaylandScreen*>(targetScreen->handle());
        if (waylandScreen) {
            output = waylandScreen->output();
        }
    }

    // Keep the QByteArray alive for the duration of the call — constData() returns
    // a pointer into the QByteArray, which must not be a dangling temporary.
    const QByteArray scopeUtf8 = scope.toUtf8();
    m_layerSurface = zwlr_layer_shell_v1_get_layer_surface(shell, m_wlSurface, output, static_cast<uint32_t>(layer),
                                                           scopeUtf8.constData());

    zwlr_layer_surface_v1_add_listener(m_layerSurface, &s_layerSurfaceListener, this);

    // Connect to LayerSurface::propertiesChanged() so that property changes
    // made after show() are immediately pushed to the compositor.
    QVariant surfaceVar = qwindow->property(LayerSurfaceProps::Surface);
    auto* layerSurface = surfaceVar.value<LayerSurface*>();
    if (!layerSurface && surfaceVar.isValid()) {
        qCWarning(lcLayerShellWindow) << "LayerSurface property is set but extracted pointer is null —"
                                      << "possible metatype registration failure for LayerSurface*";
    }
    if (layerSurface) {
        connect(layerSurface, &LayerSurface::propertiesChanged, this, [this]() {
            if (m_layerSurface && m_configured) {
                applyProperties();
                if (m_wlSurface) {
                    wl_surface_commit(m_wlSurface);
                }
                // Recalculate position when anchors/margins change (e.g. zone selector
                // position update, OSD centering) so mapFromGlobal stays accurate.
                updatePosition();
            }
        });
    }

    // If the compositor removes the layer-shell global (crash/restart), stop
    // issuing protocol requests on our now-stale zwlr_layer_surface_v1.
    // Store the callback ID so we can deregister in the destructor to prevent UAF.
    m_globalRemovedCallbackId = integration->addGlobalRemovedCallback([this]() {
        qCWarning(lcLayerShellWindow) << "Layer-shell global removed — destroying stale surface";
        if (m_layerSurface) {
            zwlr_layer_surface_v1_destroy(m_layerSurface);
            m_layerSurface = nullptr;
        }
    });

    // Apply all properties
    applyProperties();

    // Initial commit to get a configure event
    wl_surface_commit(m_wlSurface);

    // Flush immediately so the compositor receives the new surface before the
    // event loop processes other work. Without this, the commit sits in the
    // client-side write buffer while QML compilation or scene graph init blocks
    // the main thread, delaying the configure response.
    if (integration->display()) {
        wl_display_flush(integration->display()->wl_display());
    }

    qCDebug(lcLayerShellWindow) << "Created layer surface:" << scope << "layer=" << layer
                                << "screen=" << (targetScreen ? targetScreen->name() : QStringLiteral("null"));
}

LayerShellWindow::~LayerShellWindow()
{
    // Deregister the global-removed callback to prevent UAF — if the compositor
    // removes the global after this window is destroyed, the lambda would fire
    // with a dangling `this` pointer.
    if (m_integration && m_globalRemovedCallbackId != 0) {
        m_integration->removeGlobalRemovedCallback(m_globalRemovedCallbackId);
    }
    // Safe even after handleClosed() — that path nulls m_layerSurface after
    // destroying it, so this check correctly skips the double-destroy.
    if (m_layerSurface) {
        zwlr_layer_surface_v1_destroy(m_layerSurface);
    }
}

bool LayerShellWindow::isExposed() const
{
    return m_configured;
}

void LayerShellWindow::applyConfigure()
{
    if (!m_waylandWindow) {
        return;
    }

    // applyConfigure is called by Qt's render thread when it's ready to draw
    // a frame at the configured size. This is the right place to:
    //   1. Resize the QWindow via resizeFromApplyConfigure (the no-clamp path
    //      that pairs with the pending configure serial).
    //   2. ack_configure the most recent compositor configure.
    //   3. wl_surface_commit so the post-render buffer is attached at the
    //      newly-configured size in the same frame as the ack.
    //
    // Doing all three in lockstep here is what makes the resize visually take
    // effect: the buffer Qt is about to paint now matches the surface size we
    // ack to the compositor. Acking earlier (in handleConfigure) attached the
    // previous frame's buffer to the new-size surface — visually broken.
    // Accept any configure with at least one non-zero axis. The compositor
    // legitimately sends (W, 0) for a horizontally-anchored surface where
    // it controls only the width axis — requiring BOTH > 0 would drop the
    // configure entirely and leave the surface permanently un-acked
    // (subsequent configures keep being treated as pending). computeConfigureSize
    // handles the per-axis fallback to qwindow's current size.
    if (m_hasPendingConfigure && (m_pendingWidth > 0 || m_pendingHeight > 0)) {
        QWindow* qwindow = m_waylandWindow->window();
        if (qwindow) {
            const QSize newSize = computeConfigureSize(m_pendingWidth, m_pendingHeight);
            if (newSize != qwindow->size()) {
                m_waylandWindow->resizeFromApplyConfigure(newSize);
            }
        }
        if (m_layerSurface) {
            zwlr_layer_surface_v1_ack_configure(m_layerSurface, m_pendingSerial);
        }
        m_hasPendingConfigure = false;
    }

    applyProperties();

    if (m_wlSurface) {
        wl_surface_commit(m_wlSurface);
    }
}

void LayerShellWindow::setWindowGeometry(const QRect& rect)
{
    if (!m_waylandWindow || !m_waylandWindow->window()) {
        return;
    }
    if (m_layerSurface) {
        // Re-send the full layer-shell state — anchors / layer / exclusive
        // zone / keyboard / margins / set_size — and commit. set_size alone
        // is fine per the wlr-layer-shell spec ("set_size is final"), but
        // re-applying the full state on every client-initiated resize keeps
        // the protocol-side state in sync with QWindow and avoids edge
        // cases where the compositor's view of margins / anchors falls out
        // of sync after a series of client-side mutations.
        Q_UNUSED(rect)
        applyProperties();

        if (m_wlSurface) {
            wl_surface_commit(m_wlSurface);
        }
    }
}

void LayerShellWindow::applyProperties()
{
    if (!m_layerSurface) {
        return;
    }
    if (!m_waylandWindow || !m_waylandWindow->window()) {
        return;
    }

    QWindow* qwindow = m_waylandWindow->window();

    // Anchors
    int anchors = qwindow->property(LayerSurfaceProps::Anchors).toInt();
    zwlr_layer_surface_v1_set_anchor(m_layerSurface, static_cast<uint32_t>(anchors));

    // Layer — set_layer() requires protocol v2+; the initial layer is set at
    // creation time, but this allows changing it after show() (like setScope).
    if (m_integration && m_integration->boundVersion() >= 2) {
        int layer = qwindow->property(LayerSurfaceProps::Layer).toInt();
        zwlr_layer_surface_v1_set_layer(m_layerSurface, static_cast<uint32_t>(layer));
    }

    // Exclusive zone
    int exclusiveZone = qwindow->property(LayerSurfaceProps::ExclusiveZone).toInt();
    zwlr_layer_surface_v1_set_exclusive_zone(m_layerSurface, exclusiveZone);

    // Keyboard interactivity — on_demand (value 2) requires protocol v4+.
    // If the compositor only supports v1-v3, fall back to none (value 0) to
    // avoid sending an unrecognized enum value that may cause a protocol error.
    int keyboard = qwindow->property(LayerSurfaceProps::Keyboard).toInt();
    if (keyboard == 2 && m_integration && m_integration->boundVersion() < 4) {
        qCWarning(lcLayerShellWindow) << "Compositor supports layer-shell v" << m_integration->boundVersion()
                                      << "— on_demand keyboard interactivity requires v4, falling back to none";
        keyboard = 0;
    }
    zwlr_layer_surface_v1_set_keyboard_interactivity(m_layerSurface, static_cast<uint32_t>(keyboard));

    // Margins
    int marginLeft = qwindow->property(LayerSurfaceProps::MarginsLeft).toInt();
    int marginTop = qwindow->property(LayerSurfaceProps::MarginsTop).toInt();
    int marginRight = qwindow->property(LayerSurfaceProps::MarginsRight).toInt();
    int marginBottom = qwindow->property(LayerSurfaceProps::MarginsBottom).toInt();
    zwlr_layer_surface_v1_set_margin(m_layerSurface, marginTop, marginRight, marginBottom, marginLeft);

    // Size — use 0 for axes anchored to both edges; clamp to avoid uint32_t wrap on negative.
    //
    // Source of truth precedence: caller-set DesiredWidth/DesiredHeight properties
    // (via LayerSurface::setDesiredSize) override `qwindow->size()`. This decouples
    // client-initiated layer-shell resizes from `QWindow::resize()`, which is silently
    // clamped against Qt's min/max sizing on Qt 6 (QTBUG-118604) and so cannot reliably
    // grow a layer-shell surface from app code. The configure that comes back from the
    // compositor in response to the new set_size then drives the QWindow size via
    // resizeFromApplyConfigure (see handleConfigure).
    QSize sizeForLayerShell = qwindow->size();
    const int desiredW = qwindow->property(LayerSurfaceProps::DesiredWidth).toInt();
    const int desiredH = qwindow->property(LayerSurfaceProps::DesiredHeight).toInt();
    if (desiredW > 0) {
        sizeForLayerShell.setWidth(desiredW);
    }
    if (desiredH > 0) {
        sizeForLayerShell.setHeight(desiredH);
    }
    auto [w, h] = LayerSurface::computeLayerSize(LayerSurface::Anchors::fromInt(anchors), sizeForLayerShell);
    zwlr_layer_surface_v1_set_size(m_layerSurface, w, h);

    if (m_integration && m_integration->boundVersion() >= 5) {
        int exclusiveEdge = qwindow->property(LayerSurfaceProps::ExclusiveEdge).toInt();
        zwlr_layer_surface_v1_set_exclusive_edge(m_layerSurface, static_cast<uint32_t>(exclusiveEdge));
    }
}

QSize LayerShellWindow::computeConfigureSize(uint32_t width, uint32_t height) const
{
    // Layer-shell sizing contract:
    //   - Axis anchored to both edges: client sent set_size(0,_), compositor
    //     decides the size — accept the configure value.
    //   - Axis NOT doubly-anchored: client sent an explicit set_size(W,H), and
    //     the compositor's configure echoes it back. Accept the configure value
    //     too — it's the source of truth for what size the layer surface
    //     actually is. Older logic preferred `qwindow->size()` here on the
    //     theory that the app already knew what it asked for, but with the
    //     LayerSurface::setDesiredSize path the desired size lives in window
    //     PROPERTIES rather than the QWindow geometry. Reading from
    //     `qwindow->size()` then returns the stale pre-resize value and we'd
    //     skip the resizeFromApplyConfigure that would actually grow the
    //     window. Trusting the configure makes the protocol the single source
    //     of truth on every axis.
    //
    // The one nuance preserved from the older logic: if the compositor returns
    // 0 on an axis (typically only happens for fully-anchored screen-spanning
    // surfaces while the output is still being initialised), fall back to the
    // app's last-known size to avoid producing a zero-size surface.
    QWindow* qwindow = m_waylandWindow->window();
    if (!qwindow) {
        return QSize(static_cast<int>(width), static_cast<int>(height));
    }

    const int appW = qMax(0, qwindow->width());
    const int appH = qMax(0, qwindow->height());
    const int newW = (width > 0) ? static_cast<int>(width) : appW;
    const int newH = (height > 0) ? static_cast<int>(height) : appH;
    return QSize(newW, newH);
}

void LayerShellWindow::updatePosition()
{
    if (!m_waylandWindow) {
        return;
    }
    // Layer-shell surfaces are positioned by the compositor based on anchors, margins,
    // and the output geometry. Qt's QWindow doesn't know this position, so
    // mapFromGlobal() returns wrong local coordinates (assumes 0,0).
    //
    // Calculate the expected position and set it via repositionFromApplyConfigure()
    // which updates Qt's internal geometry without triggering a protocol roundtrip.
    QWindow* qwindow = m_waylandWindow->window();
    if (!qwindow) {
        return;
    }
    QScreen* screen = qwindow->screen();
    if (!screen) {
        return;
    }

    // Note: we use screen->geometry() (full output rect) here. For surfaces with
    // exclusiveZone=-1 (overlays), this is correct — they ignore other surfaces'
    // exclusive zones and stretch to full output edges.
    // For exclusiveZone=0 (geometry sensors), the compositor actually positions the
    // surface within the *available* area (pushed by panels), so this calculation
    // may be slightly off. This is acceptable because the geometry sensor is invisible
    // and only cares about its configure size, not mapFromGlobal accuracy.
    const QRect screenGeom = screen->geometry();
    int anchors = qwindow->property(LayerSurfaceProps::Anchors).toInt();
    int mLeft = qwindow->property(LayerSurfaceProps::MarginsLeft).toInt();
    int mTop = qwindow->property(LayerSurfaceProps::MarginsTop).toInt();
    int mRight = qwindow->property(LayerSurfaceProps::MarginsRight).toInt();
    int mBottom = qwindow->property(LayerSurfaceProps::MarginsBottom).toInt();
    int winW = qwindow->width();
    int winH = qwindow->height();

    bool anchorL = anchors & LayerSurface::AnchorLeft;
    bool anchorR = anchors & LayerSurface::AnchorRight;
    bool anchorT = anchors & LayerSurface::AnchorTop;
    bool anchorB = anchors & LayerSurface::AnchorBottom;

    // X position: the compositor places the surface based on horizontal anchors + margins.
    // anchorL && anchorR produces the same position as anchorL alone (left margin),
    // so we only need to check anchorL first.
    int x;
    if (anchorL) {
        x = screenGeom.x() + mLeft;
    } else if (anchorR) {
        x = screenGeom.x() + screenGeom.width() - winW - mRight;
    } else {
        x = screenGeom.x() + (screenGeom.width() - winW) / 2;
    }

    // Y position: same simplification for vertical anchors.
    int y;
    if (anchorT) {
        y = screenGeom.y() + mTop;
    } else if (anchorB) {
        y = screenGeom.y() + screenGeom.height() - winH - mBottom;
    } else {
        y = screenGeom.y() + (screenGeom.height() - winH) / 2;
    }

    // repositionFromApplyConfigure() was introduced in Qt 6.5.0.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    m_waylandWindow->repositionFromApplyConfigure(QPoint(x, y));
#else
    Q_UNUSED(x)
    Q_UNUSED(y)
#endif
}

void LayerShellWindow::handleConfigure(void* data, struct zwlr_layer_surface_v1* /*surface*/, uint32_t serial,
                                       uint32_t width, uint32_t height)
{
    auto* self = static_cast<LayerShellWindow*>(data);
    if (!self->m_layerSurface) {
        return; // Surface was invalidated (global removed)
    }
    if (!self->m_waylandWindow) {
        return;
    }

    // Just stash the configure — DO NOT resize, ack, or commit here. The
    // compositor sends configure events when the surface state should change,
    // but the client should only ack + commit when it has a buffer ready at
    // the new size. Qt's render thread drives that timing through the
    // applyConfigure() override, which is invoked when a new buffer is being
    // prepared. Acking + committing here would attach the *previous-frame*
    // buffer (still at the old size) to the new-size surface, which is what
    // produces the "surface grew on the protocol but content didn't" symptom.
    //
    // QWaylandWindow::applyConfigureWhenPossible queues this for whenever the
    // render thread is ready (synchronously inline if it can paint now,
    // deferred via a render-thread hop otherwise). It's the same pattern Qt's
    // xdg-shell QPA and Quickshell's layer-shell QPA both use.
    self->m_configured = true;
    self->m_pendingWidth = width;
    self->m_pendingHeight = height;
    self->m_pendingSerial = serial;
    self->m_hasPendingConfigure = true;

    // Schedule applyConfigure via Qt's render-thread sync. If the window isn't
    // exposed yet (first configure on a new surface) call applyConfigure
    // directly so the surface gets its first buffer; afterwards the render
    // thread drives subsequent applies.
    QWindow* qwindow = self->m_waylandWindow->window();
    if (qwindow && qwindow->isExposed()) {
        self->m_waylandWindow->applyConfigureWhenPossible();
    } else {
        self->applyConfigure();
    }

    // Calculate the window's screen position from anchors/margins/output so
    // QWindow::mapFromGlobal() returns correct local coordinates for hit testing.
    self->updatePosition();

    // Trigger Qt's exposure state update. QWaylandWindow::calculateExposure()
    // checks mShellSurface->isExposed() which returns m_configured (now true).
    // updateExposure() transitions QWaylandWindow::mExposed from false->true
    // and sends the expose event that starts Qt's rendering pipeline.
    // Without this call, the layer surface exists at the Wayland level but Qt
    // never paints it — mExposed stays false and isExposed() returns false.
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    self->m_waylandWindow->updateExposure();
#else
    if (qwindow) {
        QWindowSystemInterface::handleExposeEvent(qwindow, QRegion(QRect(QPoint(0, 0), qwindow->size())));
    }
#endif

    qCDebug(lcLayerShellWindow) << "Configured:" << width << "x" << height
                                << (qwindow ? qwindow->objectName() : QStringLiteral("(unknown)"));
}

void LayerShellWindow::handleClosed(void* data, struct zwlr_layer_surface_v1* surface)
{
    Q_UNUSED(surface)
    auto* self = static_cast<LayerShellWindow*>(data);
    qCDebug(lcLayerShellWindow) << "Layer surface closed by compositor";

    // Protocol requires client to destroy after closed event
    if (self->m_layerSurface) {
        zwlr_layer_surface_v1_destroy(self->m_layerSurface);
        self->m_layerSurface = nullptr;
    }
    // Null the wl_surface to prevent dangling commits from property-change
    // signals that may fire during qwindow->close() teardown.
    // Safety: close() may re-enter applyProperties() or setWindowGeometry() via
    // signal cascades, but those methods early-return when m_layerSurface is null.
    self->m_wlSurface = nullptr;
    // Capture qwindow + null self->m_waylandWindow BEFORE qwindow->close().
    // qwindow->close() can synchronously trigger ~QWaylandWindow which
    // deletes `this` (the shell surface), so any access to `self` after
    // close() would be a use-after-free.
    QWindow* qwindow = self->m_waylandWindow->window();
    self->m_waylandWindow = nullptr;
    if (qwindow) {
        qwindow->close();
    }
}

void LayerShellWindow::attachPopup(QtWaylandClient::QWaylandShellSurface* popup)
{
    if (!m_layerSurface || !popup) {
        return;
    }

    auto role = popup->surfaceRole();
    if (auto* xdgPopup = std::any_cast<::xdg_popup*>(&role)) {
        zwlr_layer_surface_v1_get_popup(m_layerSurface, *xdgPopup);
        qCDebug(lcLayerShellWindow) << "Attached xdg_popup to layer surface";
    }
}

} // namespace PhosphorWayland
