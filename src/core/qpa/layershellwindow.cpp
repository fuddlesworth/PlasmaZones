// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layershellwindow.h"
#include "layershellintegration.h"
#include "../layersurface.h"

#include <QLoggingCategory>
#include <qpa/qwindowsysteminterface.h>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandscreen_p.h>
#include <QtWaylandClient/private/qwaylandshellintegration_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

Q_LOGGING_CATEGORY(lcLayerShellWindow, "plasmazones.qpa.layershellwindow")

namespace PlasmaZones {

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
    // Scope default ("plasmazones") is set by LayerSurface::get() — if still empty
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
    // Called by QWaylandWindow::applyConfigure() during Qt's configure cycle.
    // Use the same compositor-controlled-axis logic as handleConfigure.
    if (m_pendingWidth > 0 && m_pendingHeight > 0) {
        QWindow* qwindow = m_waylandWindow->window();
        if (qwindow) {
            const QSize newSize = computeConfigureSize(m_pendingWidth, m_pendingHeight);
            if (newSize != qwindow->size()) {
                qwindow->resize(newSize);
            }
        }
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
        QWindow* qwindow = m_waylandWindow->window();
        auto anchors = LayerSurface::Anchors::fromInt(qwindow->property(LayerSurfaceProps::Anchors).toInt());
        auto [w, h] = LayerSurface::computeLayerSize(anchors, rect.size());
        zwlr_layer_surface_v1_set_size(m_layerSurface, w, h);

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

    // Size — use 0 for axes anchored to both edges; clamp to avoid uint32_t wrap on negative
    auto [w, h] = LayerSurface::computeLayerSize(LayerSurface::Anchors::fromInt(anchors), qwindow->size());
    zwlr_layer_surface_v1_set_size(m_layerSurface, w, h);
}

QSize LayerShellWindow::computeConfigureSize(uint32_t width, uint32_t height) const
{
    // Layer-shell sizing contract:
    //   - Axis anchored to both edges (e.g. Left+Right): client sent set_size(0,_),
    //     compositor decides the width → we MUST accept the configure width.
    //   - Axis NOT doubly-anchored: client sent an explicit size → the configure
    //     echoes it back. We keep the app-specified size to avoid overwriting
    //     carefully calculated OSD/popup dimensions.
    //
    // This matters because compositors may send sizes that differ from the screen
    // geometry (e.g. KDE subtracts panel areas even with exclusiveZone=-1).
    // Blindly resizing to the configure breaks overlays that assume screen-sized windows.
    QWindow* qwindow = m_waylandWindow->window();
    if (!qwindow) {
        return QSize(static_cast<int>(width), static_cast<int>(height));
    }

    int anchors = qwindow->property(LayerSurfaceProps::Anchors).toInt();
    bool compositorControlsW = (anchors & LayerSurface::AnchorLeft) && (anchors & LayerSurface::AnchorRight);
    bool compositorControlsH = (anchors & LayerSurface::AnchorTop) && (anchors & LayerSurface::AnchorBottom);

    // For non-compositor-controlled axes, prefer the app-set size. But if the
    // app size is 0 (e.g. newly created window before first resize), accept the
    // compositor's value to avoid creating a zero-size surface.
    int appW = qMax(0, qwindow->width());
    int appH = qMax(0, qwindow->height());
    int newW = compositorControlsW ? static_cast<int>(width) : (appW > 0 ? appW : static_cast<int>(width));
    int newH = compositorControlsH ? static_cast<int>(height) : (appH > 0 ? appH : static_cast<int>(height));
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
    // exclusiveZone=-1 (all PlasmaZones overlays), this is correct — they ignore
    // other surfaces' exclusive zones and stretch to full output edges.
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

void LayerShellWindow::handleConfigure(void* data, struct zwlr_layer_surface_v1* surface, uint32_t serial,
                                       uint32_t width, uint32_t height)
{
    auto* self = static_cast<LayerShellWindow*>(data);
    if (!self->m_layerSurface) {
        return; // Surface was invalidated (global removed)
    }
    if (!self->m_waylandWindow) {
        return;
    }
    self->m_configured = true;
    self->m_pendingWidth = width;
    self->m_pendingHeight = height;

    // Resize the Qt window to the compositor-assigned size, respecting
    // compositor-controlled axes (see computeConfigureSize for details).
    // Resize BEFORE ack_configure to avoid a re-entrant paint committing
    // a stale-size buffer after ack but before our explicit commit.
    QWindow* qwindow = self->m_waylandWindow->window();
    if (qwindow && width > 0 && height > 0) {
        const QSize newSize = self->computeConfigureSize(width, height);
        if (newSize != qwindow->size()) {
            qwindow->resize(newSize);
        }
    }

    // Re-apply current properties so the compositor sees up-to-date
    // anchors/margins/size immediately after the configure.
    self->applyProperties();

    // Ack + commit in the same frame: the protocol requires the client to
    // ack the configure and commit a buffer with the new size atomically.
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    if (self->m_wlSurface) {
        wl_surface_commit(self->m_wlSurface);
    }

    // Calculate the window's screen position from anchors/margins/output so
    // QWindow::mapFromGlobal() returns correct local coordinates for hit testing.
    self->updatePosition();

    // Trigger Qt's exposure state update. QWaylandWindow::calculateExposure()
    // checks mShellSurface->isExposed() which returns m_configured (now true).
    // updateExposure() transitions QWaylandWindow::mExposed from false→true
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
    QWindow* qwindow = self->m_waylandWindow->window();
    if (qwindow) {
        qwindow->close();
    }
    // Prevent stale access to the QWaylandWindow during Qt teardown —
    // signal cascades from close() may reference m_waylandWindow after
    // the underlying object is deleted.
    self->m_waylandWindow = nullptr;
}

} // namespace PlasmaZones
