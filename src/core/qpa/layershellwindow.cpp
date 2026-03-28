// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layershellwindow.h"
#include "layershellintegration.h"
#include "../layersurface.h"

#include <QLoggingCategory>
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

    // Resolve wl_output from QScreen
    QScreen* targetScreen = qwindow->screen();
    if (targetScreen) {
        auto* waylandScreen = dynamic_cast<QtWaylandClient::QWaylandScreen*>(targetScreen->handle());
        if (waylandScreen) {
            m_output = waylandScreen->output();
        }
    }

    // Read initial properties from QWindow dynamic properties
    int layer = qwindow->property(LayerSurfaceProps::Layer).toInt();
    QString scope = qwindow->property(LayerSurfaceProps::Scope).toString();
    if (scope.isEmpty()) {
        scope = QStringLiteral("plasmazones");
    }

    // Create the layer surface
    m_wlSurface = QtWaylandClient::QWaylandShellIntegration::wlSurfaceForWindow(waylandWindow);
    if (!m_wlSurface) {
        qCCritical(lcLayerShellWindow) << "wlSurfaceForWindow returned null — cannot create layer surface";
        return;
    }
    m_layerSurface = zwlr_layer_shell_v1_get_layer_surface(integration->layerShell(), m_wlSurface, m_output,
                                                           static_cast<uint32_t>(layer), scope.toUtf8().constData());

    zwlr_layer_surface_v1_add_listener(m_layerSurface, &s_layerSurfaceListener, this);

    // Connect to LayerSurface::propertiesChanged() so that property changes
    // made after show() are immediately pushed to the compositor.
    QVariant surfaceVar = qwindow->property(LayerSurfaceProps::Surface);
    auto* layerSurface = surfaceVar.value<LayerSurface*>();
    if (layerSurface) {
        connect(layerSurface, &LayerSurface::propertiesChanged, this, [this]() {
            if (m_layerSurface && m_configured) {
                applyProperties();
                if (m_wlSurface) {
                    wl_surface_commit(m_wlSurface);
                }
            }
        });
    }

    // Apply all properties
    applyProperties();

    // Initial commit to get a configure event
    wl_surface_commit(m_wlSurface);

    qCDebug(lcLayerShellWindow) << "Created layer surface:" << scope << "layer=" << layer
                                << "screen=" << (targetScreen ? targetScreen->name() : QStringLiteral("null"));
}

LayerShellWindow::~LayerShellWindow()
{
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
    // Re-read properties in case they changed since creation
    applyProperties();

    // Commit so the compositor sees the updated properties.
    // Layer-shell property changes are double-buffered and require a commit.
    if (m_wlSurface) {
        wl_surface_commit(m_wlSurface);
    }
}

void LayerShellWindow::setWindowGeometry(const QRect& rect)
{
    if (m_layerSurface) {
        QWindow* qwindow = m_waylandWindow->window();
        int anchors = qwindow->property(LayerSurfaceProps::Anchors).toInt();
        auto [w, h] = computeLayerSize(anchors, rect.size());
        zwlr_layer_surface_v1_set_size(m_layerSurface, w, h);

        if (m_wlSurface) {
            wl_surface_commit(m_wlSurface);
        }
    }
}

std::pair<uint32_t, uint32_t> LayerShellWindow::computeLayerSize(int anchors, const QSize& windowSize)
{
    bool anchoredH = (anchors & LayerSurface::AnchorLeft) && (anchors & LayerSurface::AnchorRight);
    bool anchoredV = (anchors & LayerSurface::AnchorTop) && (anchors & LayerSurface::AnchorBottom);
    uint32_t w = anchoredH ? 0 : static_cast<uint32_t>(qMax(0, windowSize.width()));
    uint32_t h = anchoredV ? 0 : static_cast<uint32_t>(qMax(0, windowSize.height()));
    return {w, h};
}

void LayerShellWindow::applyProperties()
{
    if (!m_layerSurface) {
        return;
    }

    QWindow* qwindow = m_waylandWindow->window();

    // Anchors
    int anchors = qwindow->property(LayerSurfaceProps::Anchors).toInt();
    zwlr_layer_surface_v1_set_anchor(m_layerSurface, static_cast<uint32_t>(anchors));

    // Exclusive zone
    int exclusiveZone = qwindow->property(LayerSurfaceProps::ExclusiveZone).toInt();
    zwlr_layer_surface_v1_set_exclusive_zone(m_layerSurface, exclusiveZone);

    // Keyboard interactivity
    int keyboard = qwindow->property(LayerSurfaceProps::Keyboard).toInt();
    zwlr_layer_surface_v1_set_keyboard_interactivity(m_layerSurface, static_cast<uint32_t>(keyboard));

    // Margins
    int marginLeft = qwindow->property(LayerSurfaceProps::MarginsLeft).toInt();
    int marginTop = qwindow->property(LayerSurfaceProps::MarginsTop).toInt();
    int marginRight = qwindow->property(LayerSurfaceProps::MarginsRight).toInt();
    int marginBottom = qwindow->property(LayerSurfaceProps::MarginsBottom).toInt();
    zwlr_layer_surface_v1_set_margin(m_layerSurface, marginTop, marginRight, marginBottom, marginLeft);

    // Size — use 0 for axes anchored to both edges; clamp to avoid uint32_t wrap on negative
    auto [w, h] = computeLayerSize(anchors, qwindow->size());
    zwlr_layer_surface_v1_set_size(m_layerSurface, w, h);
}

void LayerShellWindow::handleConfigure(void* data, struct zwlr_layer_surface_v1* surface, uint32_t serial,
                                       uint32_t width, uint32_t height)
{
    auto* self = static_cast<LayerShellWindow*>(data);
    self->m_configured = true;
    self->m_pendingWidth = width;
    self->m_pendingHeight = height;

    zwlr_layer_surface_v1_ack_configure(surface, serial);

    // Resize the Qt window to the compositor-assigned size.
    // The compositor sends surface-local coordinates, but QWindow::resize() works
    // in device-independent pixels. Divide by devicePixelRatio for fractional scaling.
    if (width > 0 && height > 0) {
        QWindow* qwindow = self->m_waylandWindow->window();
        const qreal dpr = qwindow->devicePixelRatio();
        qwindow->resize(qRound(static_cast<qreal>(width) / dpr), qRound(static_cast<qreal>(height) / dpr));
    }

    // Re-apply current properties and commit so the compositor sees
    // up-to-date anchors/margins/size immediately after the configure,
    // rather than waiting for the next Qt-driven applyConfigure() cycle.
    self->applyProperties();
    if (self->m_wlSurface) {
        wl_surface_commit(self->m_wlSurface);
    }

    // Trigger exposure so Qt starts rendering
    QWindow* qwindow = self->m_waylandWindow->window();
    if (qwindow) {
        qwindow->requestUpdate();
    }

    qCDebug(lcLayerShellWindow) << "Configured:" << width << "x" << height;
}

void LayerShellWindow::handleClosed(void* data, struct zwlr_layer_surface_v1* surface)
{
    Q_UNUSED(surface)
    auto* self = static_cast<LayerShellWindow*>(data);
    qCDebug(lcLayerShellWindow) << "Layer surface closed by compositor";

    QWindow* qwindow = self->m_waylandWindow->window();
    if (qwindow) {
        qwindow->close();
    }
}

} // namespace PlasmaZones
